#include "base_inference.h"

#include <../WinMLBootstrap.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#define NOMINMAX
#include <windows.h>

#define SHOW_DEBUG_ENUMERATION 0

namespace fs = std::filesystem;

namespace cil {
namespace IHV {
namespace infer {

BaseInference::BaseInference(
    const std::string& model_path, const std::string& model_name,
    const std::string& ep_name, const std::string& deps_dir,
    const WindowsMLExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInferenceCommon(model_path, model_name, logger),
      ep_settings_(ep_settings),
      ep_name_(ep_name),
      deps_dir_(deps_dir) {
  SetDeviceType(ep_settings.GetDeviceType());
  if (ep_name_.find("WindowsML") != std::string::npos) {
    winrt::init_apartment();  // Required for most WinRT APIs

    if (WinMLInitialize() != S_OK)
      throw std::runtime_error("Failed to initialize WinML.");

    Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "OGA");

    struct Context {
      std::atomic<bool> download_done{false};
      std::atomic<bool> download_success{false};
      std::atomic<bool> registration_done{false};
      std::atomic<bool> registration_success{false};
      std::atomic<bool> get_paths_done{false};
      std::atomic<bool> get_paths_success{false};
      std::vector<std::pair<std::wstring, std::wstring>>
          execution_provider_paths;
    } contextData;

    // Ensure the latest execution providers are available (downloads them if
    // they aren't)
    if (WinMLDownloadExecutionProviders(
            [](void* context, HRESULT result) {
              auto& contextData = *static_cast<Context*>(context);
              contextData.download_success.store(SUCCEEDED(result));
              contextData.download_done.store(true);
            },
            &contextData) != S_OK) {
      throw std::runtime_error(
          "Failed to initiate download of execution providers");
    }

    while (!contextData.download_done.load()) std::this_thread::yield();

    if (!contextData.download_success.load()) {
      throw std::runtime_error("Failed to download execution providers");
    }

    // And register the EPs with ONNX Runtime
    if (WinMLRegisterExecutionProviders(
            [](void* context, HRESULT result) {
              auto& contextData = *static_cast<Context*>(context);
              contextData.registration_success.store(SUCCEEDED(result));
              contextData.registration_done.store(true);
            },
            &contextData) != S_OK) {
      throw std::runtime_error(
          "Failed to initiate registration of execution providers");
    }

    while (!contextData.registration_success.load()) std::this_thread::yield();

    if (!contextData.registration_success.load()) {
      throw std::runtime_error("Failed to register execution providers");
    }

    // Get the paths of the execution providers installed on this device
    if (WinMLGetExecutionProviderPaths(
            [](void* context, HRESULT result, const wchar_t** names,
               const wchar_t** paths, unsigned int count) {
              auto& contextData = *static_cast<Context*>(context);
              contextData.get_paths_success.store(SUCCEEDED(result));

              auto& execution_provider_paths =
                  contextData.execution_provider_paths;
              if (SUCCEEDED(result)) {
                execution_provider_paths.reserve(count);
                for (unsigned int i = 0; i < count; ++i) {
                  execution_provider_paths.emplace_back(std::wstring(names[i]),
                                                        std::wstring(paths[i]));
                }
              } else {
                execution_provider_paths.clear();
              }
              contextData.get_paths_done.store(true);
            },
            &contextData) != S_OK) {
      throw std::runtime_error(
          "Failed to initiate retrieval of execution provider paths");
    }

    while (!contextData.get_paths_done.load()) std::this_thread::yield();

    if (!contextData.get_paths_success.load()) {
      throw std::runtime_error("Failed to get execution provider paths");
    }

    DetectWindowsMLDevices(env, contextData.execution_provider_paths);
    if (!winml_devices_.has_value() || winml_devices_.value().empty()) {
      SetErrorMessage(
          "No WindowsML devices found. Please ensure you have the required "
          "execution providers installed and registered.");
    }
  }
}

BaseInference::~BaseInference() {
  if (WinMLGetInitializationStatus() == S_OK) {
    WinMLUninitialize();
  }
}

const API_IHV_DeviceList_t* const BaseInference::EnumerateDevices() {
  if (device_enum_ == nullptr) {
    if (ep_name_.find("WindowsML") != std::string::npos &&
        winml_devices_.has_value()) {
      auto& devices_ = winml_devices_.value();
      API_IHV_DeviceList_t* dev = AllocateDeviceList(devices_.size());
      dev->count = devices_.size();
      for (size_t i = 0; i < devices_.size(); ++i) {
        dev->device_info_data[i].device_id = i;
        auto device_name = devices_[i].ep + " " + devices_[i].type + " \"" +
                           devices_[i].name + "\"";

        std::strncpy(dev->device_info_data[i].device_name, device_name.c_str(),
                     API_IHV_MAX_DEVICE_NAME_LENGTH - 1);
        dev->device_info_data[i]
            .device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] = '\0';
      }
      device_enum_ = DeviceListPtr(dev);
      default_device_name_ = devices_.empty() ? "" : devices_[0].name;
    } else {
      // Initialize list of devices with single device and default name
      // if it was never previously initialized
      default_device_name_ =
          std::format("{}.{}", ep_name_, ep_settings_.GetDeviceType());
      device_enum_ = [&]() -> DeviceListPtr {
        API_IHV_DeviceList_t* dev = AllocateDeviceList(1);

        dev->count = 1;
        dev->device_info_data[0].device_id = 0;
        std::strncpy(dev->device_info_data[0].device_name,
                     default_device_name_.c_str(),
                     API_IHV_MAX_DEVICE_NAME_LENGTH - 1);
        dev->device_info_data[0]
            .device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] = '\0';

        // Wrap raw pointer into unique ptr
        DeviceListPtr p(dev);
        return p;
      }();
    }
  }

  return device_enum_.get();
}

void BaseInference::DetectWindowsMLDevices(
    Ort::Env& env, const std::vector<std::pair<std::wstring, std::wstring>>&
                       execution_provider_paths) {
#if SHOW_WINML_DEVICES_DEBUG || SHOW_DEBUG_ENUMERATION || 0
#define LOGGER_(level, msg) logger_(level, "[WinMLDevices] " + std::string(msg))
#else
#define LOGGER_(level, msg)
#endif

  static auto lower_case = [](const std::string& input_string) {
    std::string lower_case_str;
    std::transform(input_string.begin(), input_string.end(),
                   std::back_inserter(lower_case_str),
                   [](unsigned char c) { return std::tolower(c); });
    return lower_case_str;
  };

  auto device_vendor = lower_case(ep_settings_.GetDeviceVendor());
  auto device_ep = lower_case(ep_settings_.GetDeviceEP());

  static const std::map<std::string, std::string> ep_aliases = {
      {"OpenVINOExecutionProvider", "OpenVINO"},
  };

  static const std::set<std::string> supported_eps = {"OpenVINO"};

  std::vector<Ort::ConstEpDevice> ep_devices = env.GetEpDevices();

  std::unordered_map<std::string, std::vector<Ort::ConstEpDevice>>
      ep_device_map;
  for (const auto& device : ep_devices) {
    ep_device_map[device.EpName()].push_back(device);
  }

  static const std::vector<std::string> device_types = {"CPU", "GPU", "NPU"};

  std::vector<WinMLDevice> winml_devices;

  for (const auto& [ep_name, devices] : ep_device_map) {
    auto ep = ep_aliases.contains(ep_name) ? ep_aliases.at(ep_name) : ep_name;

    LOGGER_(cil::LogLevel::kInfo, "Execution Provider: " + ep);

    for (const auto& device : devices) {
      auto type = device.Device().Type() >= 0 &&
                          device.Device().Type() < device_types.size()
                      ? device_types[device.Device().Type()]
                      : "Unknown";

      if (type == "unknown" ||
          !device_type_.empty() && (type.empty() || type != device_type_))
        continue;

      if (!supported_eps.contains(ep)) continue;

      if (!device_ep.empty() && device_ep != lower_case(ep)) continue;

      if (ep != "DirectML") {
        auto device_name =
            device.Device().Metadata().GetKeyValuePairs().contains(
                "Description")
                ? device.Device().Metadata().GetValue("Description")
                : std::string(device.Device().Vendor());
        if (device_name.empty()) device_name = std::string(device.EpVendor());

        winml_devices.emplace_back(
            WinMLDevice{ep, type, device_name, device.Device().DeviceId()});
      }
    }
  }

  static auto convertWideToUtf8 = [](const std::wstring& wstr) -> std::string {
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(),
                                    NULL, 0, NULL, NULL);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, NULL,
                        NULL);
    return str;
  };

  LOGGER_(cil::LogLevel::kInfo, "Execution Providers and their paths:");
  for (auto& [wname, wpath] : execution_provider_paths) {
    auto ep = convertWideToUtf8(wname);
    if (ep_aliases.contains(ep)) {
      ep = ep_aliases.at(ep);
    }

    auto path = convertWideToUtf8(wpath);

    LOGGER_(cil::LogLevel::kInfo, "   " + ep + " | " + path);

    auto it = std::find_if(winml_devices.begin(), winml_devices.end(),
                           [&ep](const WinMLDevice& d) { return d.ep == ep; });

    if (it != winml_devices.end()) continue;

    if (!device_ep.empty() && device_ep != lower_case(ep)) continue;

    if (ep == "OpenVINO") {
      if (device_type_.empty() || device_type_ == "NPU")
        winml_devices.emplace_back(WinMLDevice{ep, "NPU", "Intel", 0});
      if (device_type_.empty() || device_type_ == "GPU")
        winml_devices.emplace_back(WinMLDevice{ep, "GPU", "Intel", 1});
    }
  }

  for (size_t i = 0; i < winml_devices.size(); ++i) {
    for (size_t j = i + 1; j < winml_devices.size();) {
      if (winml_devices[i].ep == winml_devices[j].ep &&
          winml_devices[i].type == winml_devices[j].type &&
          winml_devices[i].name == winml_devices[j].name &&
          winml_devices[i].id == winml_devices[j].id) {
        winml_devices.erase(winml_devices.begin() + j);
      } else {
        ++j;
      }
    }
  }

  if (!winml_devices.empty() && !device_vendor.empty()) {
    winml_devices.erase(
        std::remove_if(winml_devices.begin(), winml_devices.end(),
                       [&device_vendor](const WinMLDevice& device) {
                         return lower_case(device.name).find(device_vendor) ==
                                std::string::npos;
                       }),
        winml_devices.end());
  }

  // Log final device ids
  LOGGER_(cil::LogLevel::kInfo, "Available devices:");
  for (const auto& device : winml_devices) {
    LOGGER_(cil::LogLevel::kInfo, "   " + device.ep + " | " + device.type +
                                      " | " + device.name +
                                      " | ID: " + std::to_string(device.id));
  }

  winml_devices_ = winml_devices;
}

void BaseInference::AssignModelForDevices() {
  if (!winml_devices_.has_value() || winml_devices_->empty()) {
    logger_(LogLevel::kError, "No WinML devices found");
    error_message_ = "No WinML devices found";
    return;
  }
  // Get the directory of model_file_path
  const auto model_parent_path = fs::path(model_path_).parent_path();
  auto model_folder = fs::current_path().append(model_parent_path.string());

  // Based on initial device enumeration, let's find if the the model folder
  // contains the model for each EP/Type Initially let's check current model
  // directory, whether it already contains genai_config.json file.

  struct AvailableModel {
    std::string path;
    std::set<std::string> eps;
    std::optional<std::string> type;  // Optional type, if needed
  };

  std::vector<AvailableModel> available_models;
  std::set<std::string> available_providers;

  auto get_available_providers = [&available_models, &available_providers,
                                  this](const fs::path& model_folder) {
    auto genai_config_path = model_folder / "genai_config.json";
    if (std::filesystem::exists(genai_config_path)) {
      try {
        std::ifstream genai_config_in{genai_config_path};
        nlohmann::json genai_config_json;
        genai_config_in >> genai_config_json;
        genai_config_in.close();

        auto full_path = model_folder;
        std::set<std::string> execution_providers;
        std::optional<std::string> type;

        auto find_provider_options = [&available_models, &available_providers,
                                      &execution_providers,
                                      &type](auto& session_options) {
          if (session_options.contains("provider_options")) {
            for (auto& option : session_options["provider_options"]) {
              if (option.contains("dml")) {
                available_providers.insert("DirectML");
                execution_providers.insert("DirectML");
              } else if (option.contains("OpenVINO")) {
                if (option["OpenVINO"].contains("device_type"))
                  type = option["OpenVINO"]["device_type"];

                available_providers.insert("OpenVINO");
                execution_providers.insert("OpenVINO");
              } else if (option.contains("qnn")) {
                if (option["qnn"].contains("backend_path"))
                  type = option["qnn"]["backend_path"] == "QnnHtp.dll" ? "NPU"
                                                                       : "CPU";
                available_providers.insert("QNN");
                execution_providers.insert("QNN");
              } else if (option.contains("VitisAI")) {
                available_providers.insert("VitisAI");
                execution_providers.insert("VitisAI");
              } else if (option.contains("NvTensorRtRtx")) {
                available_providers.insert("NvTensorRTRTX");
                execution_providers.insert("NvTensorRTRTX");
              }
            }
          }
        };

        // Recursive function to find all session_options in the config
        std::function<void(const nlohmann::json&)> find_all_session_options;
        find_all_session_options = [&](const nlohmann::json& node) {
          if (node.is_object()) {
            if (node.contains("session_options")) {
              auto& session_options = node["session_options"];
              find_provider_options(session_options);
            }
            for (auto& item : node.items()) {
              find_all_session_options(item.value());
            }
          } else if (node.is_array()) {
            for (auto& item : node) {
              find_all_session_options(item);
            }
          }
        };

        // Call the recursive function on the model section
        find_all_session_options(genai_config_json["model"]);

        available_models.push_back(
            AvailableModel{full_path.string(), execution_providers, type});
      } catch (const std::exception& e) {
        logger_(LogLevel::kFatal,
                "Failed to read genai_config.json: " + std::string(e.what()));
      }
    }
  };

  std::set<std::string> seen_paths;

  get_available_providers(model_folder);
  for (const auto& device : winml_devices_.value()) {
    auto subfolder = model_folder / device.type / device.ep;
    if (seen_paths.find(subfolder.string()) == seen_paths.end()) {
      seen_paths.insert(subfolder.string());
      get_available_providers(subfolder);
    }
  }

  // Assign a model to device id
  for (auto& device : winml_devices_.value()) {
    if (std::find(available_providers.begin(), available_providers.end(),
                  device.ep) == available_providers.end())
      continue;

    std::vector<std::pair<std::string, std::string>> model_paths;
    for (const auto& model : available_models) {
      if (model.eps.find(device.ep) != model.eps.end()) {
        model_paths.push_back({model.path, model.type.value_or("")});
      }
    }
    // We might have multiple models for the EP, we need to filter by type, if
    // the type is part of the path or type was specified in the
    // genai_config.json. Otherwise, we just take the first one.
    bool foundByType = false;
    bool foundByTypeInPath = false;
    for (const auto& [path, type] : model_paths) {
      if (type == device.type) {
        device.model_path = path;
        foundByType = true;
        break;
      }
      if (path.find(device.type) != std::string::npos) {
        device.model_path = path;
        foundByTypeInPath = true;
        break;
      }
    }
    // if not found by type, just take the first one
    if (!foundByType && !foundByTypeInPath && !model_paths.empty()) {
      device.model_path = model_paths[0].first;
    } else if (model_paths.empty()) {
      logger_(LogLevel::kError, "No model found for device: " + device.name);
      return;
    }
  }

#if SHOW_FILTERED_DEVICES || SHOW_DEBUG_ENUMERATION || 0
  logger_(LogLevel::kInfo, "Filtered devices by available models:");
  for (const auto& device : winml_devices_.value()) {
    if (device.model_path.empty()) continue;
    logger_(LogLevel::kInfo, "   " + device.ep + " | " + device.type + " | " +
                                 device.name +
                                 " | ID: " + std::to_string(device.id) +
                                 " | Model Path: " + device.model_path);
  }
#endif
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
