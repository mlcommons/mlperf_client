#include "preparation_stage.h"

#include "api_handler.h"
#include "execution_provider.h"
#include "file_signature_verifier.h"
#include "scenario_data_provider.h"
#include "utils.h"

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
                             std::string& library_path);

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
      continue;
    }

    if (((!ep_config.contains("device_id") || ep_config["device_id"] < 0) && !device_id_.has_value()) ||
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

  if (enumerate_only_) return true;

  prepared_eps_ = adjusted_eps;

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
    const std::string& ep_name, std::string& library_path) {
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
  CanLoadLibrary(ep_type, ep_name, library_path);
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

#if WITH_QNN
      if (ep_name == "QNN") {
        SetupQNN(ep_config_json, was_visited);
      }
#endif()

      auto can_load_library = [&]() {
        CanLoadLibrary(ep_type, ep_name, library_path);
      };

      auto unpack_library_if_needed =
          [&](const std::vector<Unpacker::Asset>& assets) {
            UnpackLibraryIfNeeded(assets, ep_type, ep_name, library_path);
          };

      if (!empty_library_path) {
        if (!fs::exists(library_path)) {
          std::string error = "IHV";
          error += " library path does not exist: " + library_path;
          throw std::runtime_error(error);
        }
        can_load_library();
      } else {
        switch (ep_type) {
#if WITH_IHV_NATIVE_OPENVINO
          case kIHVNativeOpenVINO:
            unpack_library_if_needed({kNativeOpenVINO});
            break;
#endif
#if WITH_IHV_ORT_GENAI
          case kIHVOrtGenAI:
            unpack_library_if_needed({kOrtGenAI});
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
