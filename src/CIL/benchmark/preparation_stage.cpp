#include "preparation_stage.h"

#include <shared_mutex>

#include "api_handler.h"
#include "execution_provider.h"
#include "file_signature_verifier.h"
#include "llm/portable_python.h"
#include "runner.h"
#include "scenario_data_provider.h"
#include "utils.h"

#ifdef __APPLE__
#include <TargetConditionals.h>

#include "apple/apple_utils.h"
#endif  //__APPLE__

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {

struct DeviceEnumerationCache {
  struct Ep {
    std::string library_path_;
    std::string ep_name_;
    std::string config_hash_;  // Hash of relevant ep_config

    bool operator==(const Ep& other) const = default;

    explicit Ep(const std::string& library_path, const std::string& ep,
                const nlohmann::json& ep_config)
        : library_path_(library_path), ep_name_(ep) {
      // is_string() guards against nlohmann::detail::type_error (derives from
      // std::invalid_argument) when a field is present but null or non-string.
      std::string hash;
      auto append = [&](const char* field, const char* prefix) {
        auto it = ep_config.find(field);
        if (it != ep_config.end() && it->is_string())
          hash += std::string(prefix) + it->get<std::string>() + ";";
      };
      append("device_type", "dt:");
      append("device_vendor", "dv:");
      append("device_ep", "de:");
      config_hash_ = hash;
    }

    struct Hash {
      size_t operator()(const Ep& key) const {
        size_t h1 = std::hash<std::string>{}(key.library_path_);
        size_t h2 = std::hash<std::string>{}(key.ep_name_);
        size_t h3 = std::hash<std::string>{}(key.config_hash_);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
      }
    };
  };

  struct Result {
    cil::API_Handler::DeviceList devices;
    std::string log;
  };

  std::shared_mutex mutex_;
  std::unordered_map<Ep, Result, Ep::Hash> cache_;

  std::optional<Result> Get(const std::string& library_path,
                            const std::string& ep_name,
                            const nlohmann::json& ep_config) {
    Ep ep{library_path, ep_name, ep_config};

    std::shared_lock lock(mutex_);

    if (auto it = cache_.find(ep); it != cache_.end()) {
      return it->second;
    }

    return std::nullopt;
  }

  void Set(const std::string& library_path, const std::string& ep_name,
           const nlohmann::json& ep_config,
           const cil::API_Handler::DeviceList& devices,
           const std::string& log) {
    Ep ep{library_path, ep_name, ep_config};

    std::unique_lock lock(mutex_);

    cache_[ep] = Result{devices, log};
  }

  void Clear() {
    std::unique_lock lock(mutex_);
    cache_.clear();
  }
};

static DeviceEnumerationCache& GetDeviceEnumCache() {
  static DeviceEnumerationCache cache;
  return cache;
}

class PreparationStage::Impl {
 public:
  static bool Run(PreparationStage& stage,
                  const ScenarioConfig& scenario_config,
                  std::vector<std::pair<ExecutionProviderConfig, std::string>>&
                      prepared_eps,
                  bool enumerate_only, std::optional<int> device_id,
                  const std::optional<std::string>& python_search_dir,
                  const std::optional<std::string>& python_configured_dir) {
    prepared_eps.clear();
    Impl impl{
        stage,     scenario_config,   prepared_eps,         enumerate_only,
        device_id, python_search_dir, python_configured_dir};
    return impl.Run();
  }

 private:
  Impl(PreparationStage& stage, const ScenarioConfig& scenario_config,
       std::vector<std::pair<ExecutionProviderConfig, std::string>>&
           prepared_eps,
       bool enumerate_only, std::optional<int> device_id,
       const std::optional<std::string>& python_search_dir,
       const std::optional<std::string>& python_configured_dir)
      : logger_(stage.GetLogger()),
        unpacker_(stage.GetUnpacker()),
        ep_dependencies_manager_(stage.GetEPDependenciesManager()),
        scenario_config_(scenario_config),
        prepared_eps_(prepared_eps),
        python_search_dir_(python_search_dir),
        python_configured_dir_(python_configured_dir),
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

  const std::optional<std::string> python_search_dir_;
  const std::optional<std::string> python_configured_dir_;

  std::unordered_set<std::string> failed_ep_names_;
  std::unordered_map<std::string, int> prepared_ep_names_;
  std::map<Unpacker::Asset, bool> unpacked_map_;
  std::map<EP, std::set<std::string>> eps_libraries_;

  const bool enumerate_only_;
  const std::optional<int> device_id_;
};

bool PreparationStage::Run(const ScenarioConfig& scenario_config,
                           ScenarioData& scenario_data,
                           const ReportProgressCb&) {
  std::optional<std::string> python_search_dir;
  std::optional<std::string> python_configured_dir;
  if (const std::string& python_path =
          config_.GetSystemConfig().GetPythonPath();
      !enumerate_only_ && scenario_config.IsAgentic()) {
    if (!scenario_data.input_file_paths.empty())
      python_search_dir = fs::path(scenario_data.input_file_paths.front())
                              .parent_path()
                              .string();
    python_configured_dir = python_path;
  }

  return Impl::Run(*this, scenario_config, scenario_data.prepared_eps,
                   enumerate_only_, device_id_, python_search_dir,
                   python_configured_dir);
}

void PreparationStage::ClearDeviceEnumerationCache() {
  GetDeviceEnumCache().Clear();
}

bool PreparationStage::Impl::PrepareIHVDevicesIfNeeded(
    const EP ep_type, const std::string_view& ep_library_path) {
  std::vector<std::pair<ExecutionProviderConfig, std::string>> adjusted_eps;
  int valid_eps = 0;

  auto& device_enum_cache = GetDeviceEnumCache();

  for (const auto& [ep, library_path] : prepared_eps_) {
    const auto& ep_name = ep.GetName();
    if (NameToEP(ep_name) != ep_type || library_path != ep_library_path) {
      adjusted_eps.emplace_back(ep, library_path);
      continue;
    }

    auto ep_config = ep.GetConfig();

    std::string error;
    std::string log;
    API_Handler::DeviceList devices;

    // Check if we have cached enumeration results for this IHV + settings
    // Device enumeration does not depend on model type, only on IHV and
    // device settings (device_type, device_vendor, device_ep)
    auto cached_result =
        device_enum_cache.Get(library_path, ep_name, ep_config);
    if (cached_result.has_value()) {
      devices = cached_result->devices;
      log = cached_result->log;
      LOG4CXX_DEBUG(logger_, "Using cached device enumeration for " << ep_name);
    } else {
      devices = API_Handler::EnumerateDevices(library_path, ep_name,
                                              scenario_config_.GetName(),
                                              ep_config, error, log);

      if (error.empty()) {
        device_enum_cache.Set(library_path, ep_name, ep_config, devices, log);
      }
    }

    // Always surface the IHV's info log (if any) so diagnostics emitted
    // before a failure are visible. Previously this was skipped when error
    // was non-empty, hiding all kInfo output from the IHV subprocess.
    if (!log.empty()) {
      LOG4CXX_INFO(logger_, log);
    }

    if (!error.empty()) {
      LOG4CXX_ERROR(logger_, error);
      continue;
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
      }
    }
  }

  prepared_eps_ = adjusted_eps;

  return enumerate_only_ || valid_eps > 0;
}

void PreparationStage::Impl::PrepareDeviceIdsIfNeeded() {
  auto mark_ihv_eps_as_failed = [&](const EP ep_type,
                                    const std::string& ep_library_path) {
    for (const auto& [ep, library_path] : prepared_eps_) {
      const auto& ep_name = ep.GetName();
      if (NameToEP(ep_name) != ep_type || library_path != ep_library_path)
        continue;
      --prepared_ep_names_[ep_name];
      failed_ep_names_.insert(ep_name);
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
        case EP::kIHVLlamaCpp:
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
  if (auto error = API_Handler::CanBeLoaded(library_path); !error.empty()) {
    throw std::runtime_error("Failed to load required libraries for " +
                             ep_name + "\n" + error);
  }
  eps_libraries_[ep_type].insert(library_path);
}

bool PreparationStage::Impl::Run() {
  if ((python_search_dir_ || python_configured_dir_) &&
      !EnsurePortablePythonOnPath(python_search_dir_.value_or(""),
                                  python_configured_dir_.value_or(""), logger_))
    return false;

  const auto scenario_name = scenario_config_.GetName();

  using enum Unpacker::Asset;
  using enum EP;

  for (const auto& ep : scenario_config_.GetExecutionProviders()) {
    const auto& ep_name = ep.GetName();
    const auto ep_type = NameToEP(ep_name);

    auto library_path = ep.GetLibraryPath();
    bool empty_library_path = library_path.empty();

    bool was_visited = failed_ep_names_.contains(ep_name);
    bool is_supported = BenchmarkRunner::IsEpSupportedOnThisPlatform(
        scenario_name, ep.GetFullName());

    try {
      if (!is_supported
          || empty_library_path &&
                 !ep_dependencies_manager_.PrepareDependenciesForEP(ep_name)
      ) {
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
          case kIHVWindowsML:
            unpack_library_if_needed({kWindowsML});
            break;
#endif
#if WITH_IHV_MLX
          case kIHVMLX:
            unpack_library_if_needed({kMLX});
            break;
#endif
#if WITH_IHV_GGML_METAL || WITH_IHV_GGML_VULKAN || WITH_IHV_GGML_CUDA || \
    WITH_IHV_GGML_ROCM
          case kIHVLlamaCpp: {
            std::string backend;
            if (ep_config_json.contains("backend") &&
                ep_config_json["backend"].is_string())
              backend = ep_config_json["backend"].get<std::string>();
            std::vector<Unpacker::Asset> assets;
#if WITH_IHV_GGML_METAL
            if (backend == "Metal") assets = {kLlama_cpp_Metal, kGGML_EPs};
#endif
#if WITH_IHV_GGML_VULKAN
            if (backend == "Vulkan") assets = {kLlama_cpp_Vulkan, kGGML_EPs};
#endif
#if WITH_IHV_GGML_CUDA
            if (backend == "CUDA") assets = {kLlama_cpp_CUDA, kGGML_EPs};
#endif
#if WITH_IHV_GGML_ROCM
            if (backend == "ROCm") assets = {kLlama_cpp_ROCm, kGGML_EPs};
#endif
            if (assets.empty())
              throw std::runtime_error("Unsupported llama-cpp backend: " +
                                       backend);
            unpack_library_if_needed(assets);
            break;
          }
#endif
#if WITH_IHV_DIFFUSERS
          case kIHVDiffusers: {
            std::vector<Unpacker::Asset> assets{kDiffusers};
#if WITH_IHV_DIFFUSERS_DISPATCHER
            std::string vendor;
            if (ep_config_json.contains("device_vendor") &&
                ep_config_json["device_vendor"].is_string()) {
              vendor = ep_config_json["device_vendor"].get<std::string>();
              for (auto& c : vendor) c = static_cast<char>(::toupper(c));
            } else if (ep_config_json.contains("backend") &&
                       ep_config_json["backend"].is_string()) {
              const std::string b =
                  ep_config_json["backend"].get<std::string>();
              if (b == "CUDA")
                vendor = "NVIDIA";
              else if (b == "MPS")
                vendor = "APPLE";
              else if (b == "RYZENAI")
                vendor = "AMD";
            }
#if WITH_IHV_DIFFUSERS_NVIDIA
            if (vendor == "NVIDIA") assets.push_back(kDiffusers_NVIDIA);
#endif
#if WITH_IHV_DIFFUSERS_APPLE
            if (vendor == "APPLE") assets.push_back(kDiffusers_APPLE);
#endif
#if WITH_IHV_DIFFUSERS_AMD
            if (vendor == "AMD") assets.push_back(kDiffusers_AMD);
#endif
#endif  // WITH_IHV_DIFFUSERS_DISPATCHER
            unpack_library_if_needed(assets);
            break;
          }
#endif
          default:
            throw std::runtime_error("Unrecognized IHV EP: " + ep_name);
        }
      }

      ++prepared_ep_names_[ep_name];
      prepared_eps_.emplace_back(ep.OverrideConfig(ep_config_json),
                                 library_path);
    } catch (const std::exception& e) {
      if (!was_visited) LOG4CXX_ERROR(logger_, e.what());
      failed_ep_names_.insert(ep_name);
    }
  }

  PrepareDeviceIdsIfNeeded();

  std::erase_if(prepared_ep_names_,
                [](const auto& pair) { return pair.second <= 0; });

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
