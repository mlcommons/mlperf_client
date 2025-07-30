#include "preparation_stage.h"

#include "api_handler.h"
#include "execution_provider.h"
#include "file_signature_verifier.h"
#include "scenario_data_provider.h"
#include "utils.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include "ios/ios_utils.h"
#endif
#endif  //__APPLE__

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {
class PreparationStage::Impl {
 public:
  static bool Run(PreparationStage& stage,
                  const ScenarioConfig& scenario_config,
                  std::vector<std::pair<ExecutionProviderConfig, std::string>>&
                      prepared_eps,
                  bool enumerate_only, std::optional<int> device_id) {
    prepared_eps.clear();
    Impl impl{stage, scenario_config, prepared_eps, enumerate_only, device_id};

    return impl.Run();
  }

 private:
  Impl(PreparationStage& stage, const ScenarioConfig& scenario_config,
       std::vector<std::pair<ExecutionProviderConfig, std::string>>&
           prepared_eps,
       bool enumerate_only, std::optional<int> device_id)
      : logger_(stage.GetLogger()),
        unpacker_(stage.GetUnpacker()),
        ep_dependencies_manager_(stage.GetEPDependenciesManager()),
        scenario_config_(scenario_config),
        prepared_eps_(prepared_eps),
        enumerate_only_(enumerate_only),
        device_id_(device_id) {}

  bool PrepareIHVDevicesIfNeeded(const EP ep_type,
                                 const std::string_view& ep_library_path);

  void PrepareDeviceIdsIfNeeded();

  void UnpackLibraryIfNeeded(const std::vector<Unpacker::Asset>& assets,
                             EP ep_type, const std::string& ep_name,
                             std::string& library_path,
                             bool try_to_load = true);

  void CanLoadLibrary(EP ep_type, const std::string& ep_name,
                      const std::string& library_path);

  bool Run();

  const log4cxx::LoggerPtr& logger_;
  Unpacker& unpacker_;
  EPDependenciesManager& ep_dependencies_manager_;
  const ScenarioConfig& scenario_config_;

  std::vector<std::pair<ExecutionProviderConfig, std::string>>& prepared_eps_;
  std::unordered_set<std::string> failed_ep_names_;
  std::unordered_set<std::string> prepared_ep_names_;
  std::map<Unpacker::Asset, bool> unpacked_map_;
  std::map<EP, std::set<std::string>> eps_libraries_;

  const bool enumerate_only_;
  const std::optional<int> device_id_;
};

bool PreparationStage::Run(const ScenarioConfig& scenario_config,
                           ScenarioData& scenario_data,
                           const ReportProgressCb&) {
  return Impl::Run(*this, scenario_config, scenario_data.prepared_eps,
                   enumerate_only_, device_id_);
}

bool PreparationStage::Impl::PrepareIHVDevicesIfNeeded(
    const EP ep_type, const std::string_view& ep_library_path) {
  std::vector<std::pair<ExecutionProviderConfig, std::string>> adjusted_eps;
  int valid_eps = 0;

  for (const auto& [ep, library_path] : prepared_eps_) {
    const auto& ep_name = ep.GetName();
    if (NameToEP(ep_name) != ep_type || library_path != ep_library_path) {
      adjusted_eps.emplace_back(ep, library_path);
      continue;
    }

    auto ep_config = ep.GetConfig();

    std::string error;
    std::string log;
    auto devices = API_Handler::EnumerateDevices(
        API_Handler::Type::kIHV, library_path, ep_name,
        scenario_config_.GetName(), ep_config, error, log);

    if (!error.empty()) {
      LOG4CXX_ERROR(logger_, error);
      failed_ep_names_.insert(ep_name);
      prepared_ep_names_.erase(ep_name);
      continue;
    }

    if (!log.empty()) {
      LOG4CXX_INFO(logger_, log);
    }

    if (enumerate_only_) {
      LOG4CXX_INFO(logger_, "Enumerating devices for " << ep_name);
      // display devices
      for (const auto& [device_id, device_name] : devices) {
        LOG4CXX_INFO(logger_, "\tDevice ID: " << device_id << ", Device Name: "
                                              << device_name);
      }
    }

    if (((!ep_config.contains("device_id") || ep_config["device_id"] < 0) &&
         !device_id_.has_value()) ||
        (device_id_.has_value() && device_id_.value() == -1)) {
      if (devices.empty()) {
        LOG4CXX_ERROR(logger_, "Failed to prepare " << ep_name
                                                    << " EP, no devices found");
      } else {
        for (const auto& [device_id, device_name] : devices) {
          ep_config["device_id"] = device_id;
          ep_config["device_name"] = device_name;
          auto newEP = ep.OverrideConfig(ep_config);
          adjusted_eps.emplace_back(newEP, library_path);
          valid_eps++;
        }
      }
    } else {
      int ep_device_id = device_id_.value_or(ep_config["device_id"]);

      if (ep_device_id >= 0 && ep_device_id < devices.size()) {
        if (device_id_.has_value()) {
          ep_config["device_id"] = ep_device_id;
          ep_config["device_name"] = devices[ep_device_id].device_name;

          auto newEP = ep.OverrideConfig(ep_config);

          adjusted_eps.emplace_back(newEP, library_path);
          valid_eps++;
        } else {
          adjusted_eps.emplace_back(ep, library_path);
        }
        valid_eps++;
      } else {
        LOG4CXX_ERROR(logger_, "Failed to prepare "
                                   << ep_name
                                   << " EP, device_id is out of range");
        failed_ep_names_.insert(ep_name);
        prepared_ep_names_.erase(ep_name);
      }
    }
  }

  prepared_eps_ = adjusted_eps;
  if (enumerate_only_) return true;

  return valid_eps > 0;
}

void PreparationStage::Impl::PrepareDeviceIdsIfNeeded() {
  auto mark_ihv_eps_as_failed = [&](const EP ep_type,
                                    const std::string& ep_library_path) {
    for (const auto& [ep, library_path] : prepared_eps_) {
      const auto& ep_name = ep.GetName();
      if (NameToEP(ep_name) == ep_type && library_path == ep_library_path)
        prepared_ep_names_.erase(ep_name);
    }
  };

  auto check_devices = [&](const EP ep_type,
                           const std::string& ep_library_path) {
    if (!PrepareIHVDevicesIfNeeded(ep_type, ep_library_path)) {
      mark_ihv_eps_as_failed(ep_type, ep_library_path);
    }
  };

  for (const auto& [ep_type, library_paths] : eps_libraries_) {
    for (auto& library_path : library_paths) {
      switch (ep_type) {
        case EP::kIHVOrtGenAI:
        case EP::kIHVNativeOpenVINO:
        case EP::kIHVWindowsML:
          check_devices(ep_type, library_path);
          break;
        default:
          if (enumerate_only_) {
            check_devices(ep_type, library_path);
          } else if (device_id_.has_value()) {
            // log not supported
            LOG4CXX_ERROR(logger_,
                          "Failed to prepare EPs, device_id is not "
                          "supported for this EP type");
            mark_ihv_eps_as_failed(ep_type, library_path);
          }
          break;
      }
    }
  }
}

void PreparationStage::Impl::UnpackLibraryIfNeeded(
    const std::vector<Unpacker::Asset>& assets, EP ep_type,
    const std::string& ep_name, std::string& library_path, bool try_to_load) {
  for (auto& asset : assets) {
    auto& unpacked_asset = unpacked_map_[asset];
    unpacked_asset =
        unpacker_.UnpackAsset(asset, library_path, !unpacked_asset);
    if (!unpacked_asset) {
      std::string error = "Failed to unpack ";
      error += "IHV library for " + ep_name;
      throw std::runtime_error(error);
    }
  }
  if (try_to_load) CanLoadLibrary(ep_type, ep_name, library_path);
}

void PreparationStage::Impl::CanLoadLibrary(EP ep_type,
                                            const std::string& ep_name,
                                            const std::string& library_path) {
  if (auto error =
          API_Handler::CanBeLoaded(API_Handler::Type::kIHV, library_path);
      !error.empty()) {
    throw std::runtime_error("Failed to load required libraries for " +
                             ep_name + "\n" + error);
  }
  eps_libraries_[ep_type].insert(library_path);
}

#if WITH_IHV_WIN_ML
static bool InstallWindowsMLMSIX(const std::string& library_path,
                                 const log4cxx::LoggerPtr& logger) {
  // Helper to run command hidden
  auto RunHiddenCommand = [](const std::string& cmd) -> bool {
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::string cmd_copy = "cmd.exe /C " + cmd;

    BOOL success = CreateProcessA(nullptr,
                                  &cmd_copy[0],  // Command line (mutable)
                                  nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                  nullptr, nullptr, &si, &pi);

    if (success) {
      WaitForSingleObject(pi.hProcess, INFINITE);
      DWORD exitCode;
      GetExitCodeProcess(pi.hProcess, &exitCode);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return (exitCode == 0);
    }

    return false;
  };

  // Step 1: Check if package is already installed
  std::string check_cmd =
      "powershell.exe -ExecutionPolicy Bypass -NoProfile "
      "-Command \"if (Get-AppxPackage -Name Microsoft.WindowsMLRuntime.0.3) { "
      "exit 0 } else { exit 1 }\"";

  if (RunHiddenCommand(check_cmd)) {
    return true;
  }

  // Step 2: Install the package
  auto msix_path = (fs::path(library_path).parent_path() /
                    "Microsoft.Windows.AI.MachineLearning.msix")
                       .lexically_normal();

  std::string install_cmd =
      "powershell.exe -ExecutionPolicy Bypass -NoProfile "
      "-Command \"Add-AppxPackage -Path '" +
      msix_path.string() + "'\"";

  if (!RunHiddenCommand(install_cmd)) {
    LOG4CXX_ERROR(logger, "Failed to install Windows ML MSIX package: " +
                              msix_path.string());
    return false;
  }

  return true;
}
#endif

bool PreparationStage::Impl::Run() {
  const auto scenario_name = scenario_config_.GetName();

  using enum Unpacker::Asset;
  using enum EP;

  for (const auto& ep : scenario_config_.GetExecutionProviders()) {
    const auto& ep_name = ep.GetName();
    const auto ep_type = NameToEP(ep_name);

    auto library_path = ep.GetLibraryPath();
    bool empty_library_path = library_path.empty();

    bool was_visited = failed_ep_names_.contains(ep_name);
    bool is_supported =
        utils::IsEpSupportedOnThisPlatform(scenario_name, ep_name);

    try {
      if (!is_supported ||
          empty_library_path &&
              !ep_dependencies_manager_.PrepareDependenciesForEP(ep_name)) {
        if (is_supported) {
          throw std::runtime_error("Failed to prepare " + ep_name +
                                   "dependencies, skipping...");
        } else {
          throw std::runtime_error(ep_name + " is not supported, skipping...");
        }
      }

      // We copy the config to be able to modify it
      // Regardless if the config will be altered, we will override the EP
      // config with this new one
      auto ep_config_json = ep.GetConfig();

      auto can_load_library = [&]() {
        CanLoadLibrary(ep_type, ep_name, library_path);
      };

      auto unpack_library_if_needed =
          [&](const std::vector<Unpacker::Asset>& assets,
              bool try_to_load = true) {
            UnpackLibraryIfNeeded(assets, ep_type, ep_name, library_path,
                                  try_to_load);
          };

      if (!empty_library_path) {
        if (!fs::exists(library_path)) {
          std::string error = "IHV";
          error += " library path does not exist: " + library_path;
          throw std::runtime_error(error);
        }
#if WITH_IHV_WIN_ML
        if (ep_type == kIHVWindowsML &&
            !InstallWindowsMLMSIX(library_path, logger_)) {
          throw std::runtime_error(
              "Failed to install Windows ML MSIX package, skipping...");
        }
#endif
        can_load_library();
      } else {
        switch (ep_type) {
#if WITH_IHV_NATIVE_OPENVINO
          case kIHVNativeOpenVINO:
            unpack_library_if_needed({kNativeOpenVINO});
            break;
#endif
#if WITH_IHV_NATIVE_QNN
          case kIHVNativeQNN:
            unpack_library_if_needed({kNativeQNN});
            break;
#endif
#if WITH_IHV_ORT_GENAI
          case kIHVOrtGenAI:
            unpack_library_if_needed({kOrtGenAI});
            break;
#endif
#if WITH_IHV_ORT_GENAI_RYZENAI
          case kkIHVOrtGenAIRyzenAI:
            unpack_library_if_needed({kOrtGenAIRyzenAI});
            break;
#endif
#if WITH_IHV_WIN_ML
          case kIHVWindowsML: {
            unpack_library_if_needed({kWindowsML}, false);
            if (!InstallWindowsMLMSIX(library_path, logger_)) {
              throw std::runtime_error(
                  "Failed to install Windows ML MSIX package, skipping...");
            }
            can_load_library();
          } break;
#endif
#if WITH_IHV_MLX
          case kIHVMLX:
#if !TARGET_OS_IOS
            unpack_library_if_needed({kMLX});
#else
            library_path =
                ios_utils::GetIOSLibraryPath("IHV_MLX.framework/IHV_MLX");
            can_load_library();
#endif
            break;
#endif
#if WITH_IHV_GGML_METAL
          case kIHVMetal:
#if !TARGET_OS_IOS
            unpack_library_if_needed({kGGML_Metal, kGGML_EPs});
#else
            library_path = ios_utils::GetIOSLibraryPath(
                "IHV_GGML_EPs.framework/IHV_GGML_EPs");
            can_load_library();
#endif
            break;
#endif
#if WITH_IHV_GGML_VULKAN
          case kIHVVulkan:
            unpack_library_if_needed({kGGML_Vulkan, kGGML_EPs});
            break;
#endif
#if WITH_IHV_GGML_CUDA
          case kIHVCUDA:
            unpack_library_if_needed({kGGML_CUDA, kGGML_EPs});
            break;
#endif
          default:
            throw std::runtime_error("Unrecognized IHV EP: " + ep_name);
        }
      }

      prepared_ep_names_.insert(ep_name);
      prepared_eps_.emplace_back(ep.OverrideConfig(ep_config_json),
                                 library_path);
    } catch (const std::exception& e) {
      if (!was_visited) LOG4CXX_ERROR(logger_, e.what());
      failed_ep_names_.insert(ep_name);
    }
  }

  PrepareDeviceIdsIfNeeded();

  std::stringstream not_prepared_eps_str;
  for (const auto& ep : failed_ep_names_) {
    if (prepared_ep_names_.contains(ep)) continue;

    if (std::streampos(0) != not_prepared_eps_str.tellp()) {
      not_prepared_eps_str << ", ";
    }
    not_prepared_eps_str << ep;
  }

  if (auto str = not_prepared_eps_str.str(); !str.empty()) {
    std::string error = "Following EPs used for the model ";
    error += scenario_name;
    error += " could not be prepared and will not be ";
    error +=
        enumerate_only_ ? "used for enumeration: [" : "used for inference: [";
    error += str;
    error += "].\n";
    LOG4CXX_ERROR(logger_, error);
  }

  return !prepared_ep_names_.empty() || enumerate_only_;
}

}  // namespace cil
// namespace cil
