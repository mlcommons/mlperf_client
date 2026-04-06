#include "base_inference.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include "winrt/Microsoft.Windows.AI.MachineLearning.h"

#define NOMINMAX
#include <windows.h>
#include <MddBootstrap.h>

#define SHOW_DEBUG_ENUMERATION 0

namespace fs = std::filesystem;
namespace winml = winrt::Microsoft::Windows::AI::MachineLearning;

static std::string ConvertWideToUtf8(const std::wstring& wstr) {
  int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), NULL,
                                  0, NULL, NULL);
  std::string str(count, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, NULL, NULL);
  return str;
}

void LogInstalledWinMLEPVersions(cil::Logger& logger_cb) {
  static const auto runHiddenCommandCapture =
      [](const std::string& cmd, std::string& stdout_text) -> DWORD {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
      return (DWORD)-1;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
      CloseHandle(read_pipe);
      CloseHandle(write_pipe);
      return (DWORD)-1;
    }

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;

    std::string cmd_copy = "cmd.exe /C " + cmd;

    BOOL success = CreateProcessA(nullptr, &cmd_copy[0], nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(write_pipe);

    if (!success) {
      CloseHandle(read_pipe);
      return (DWORD)-1;
    }

    std::string buffer;
    char chunk[4096];
    DWORD bytes_read = 0;
    while (ReadFile(read_pipe, chunk, sizeof(chunk), &bytes_read, nullptr) &&
           bytes_read) {
      buffer.append(chunk, chunk + bytes_read);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    size_t l = 0;
    size_t r = buffer.size();
    auto is_ws = [&](char c) {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
    };
    while (l < r && is_ws(buffer[l])) ++l;
    while (r > l && is_ws(buffer[r - 1])) --r;
    stdout_text.assign(buffer.data() + l, r - l);

    return exitCode;
  };

  std::string cmd =
      std::string("powershell.exe -ExecutionPolicy Bypass -NoProfile ") +
      "-Command \"$ErrorActionPreference='SilentlyContinue'; "
      "$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator); "
      "$pkgs = if ($isAdmin) { Get-AppxPackage -AllUsers } else { Get-AppxPackage }; "
      "$pkgs = $pkgs | Where-Object { $_.Name -like 'MicrosoftCorporationII.WinML.*' } | Sort-Object Name; "
      "foreach($p in $pkgs){ Write-Output ($p.Name + '|' + $p.Version) }\"";

  std::string out;
  runHiddenCommandCapture(cmd, out);

  if (out.empty()) {
    logger_cb(cil::LogLevel::kWarning,
              "No WindowsML EP MSIX packages detected via Get-AppxPackage.");
    return;
  }

  size_t start = 0;
  while (start < out.size()) {
    size_t end = out.find_first_of("\r\n", start);
    if (end == std::string::npos) end = out.size();
    if (end > start) {
      std::string line = out.substr(start, end - start);

      size_t i = 0, j = line.size();
      auto is_ws2 = [&](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
      };
      while (i < j && is_ws2(line[i])) ++i;
      while (j > i && is_ws2(line[j - 1])) --j;
      if (j > i) {
        std::string trimmed = line.substr(i, j - i);
        auto bar = trimmed.find('|');
        if (bar != std::string::npos) {
          std::string name = trimmed.substr(0, bar);
          std::string version = trimmed.substr(bar + 1);
          logger_cb(cil::LogLevel::kInfo,
                    std::string("WindowsML EP package: ") + name +
                        ", version=" + version);
        } else {
          logger_cb(cil::LogLevel::kInfo,
                    std::string("WindowsML EP package: ") + trimmed);
        }
      }
    }
    start = end + 1;
    if (start < out.size() && (out[start] == '\n' || out[start] == '\r'))
      ++start;
  }
}

static std::once_flag g_epRegistrationFlag;
static bool EnsureAndRegisterEPs(Ort::Env& env, cil::Logger& logger) {
  static bool initialized = false;

  std::call_once(g_epRegistrationFlag, [&env, &logger]() {
    // Initialize Windows App SDK (if not already initialized)
    const UINT32 majorMinor = 0x00010008;  // WinAppSDK 1.8
    PACKAGE_VERSION minVersion{};          // no minimum
    const HRESULT hr = MddBootstrapInitialize2(
        majorMinor,                                       // majorMinorVersion
        nullptr,                                          // versionTag (stable)
        minVersion,                                       // minVersion
        MddBootstrapInitializeOptions_OnNoMatch_ShowUI);  // options
    initialized = SUCCEEDED(hr);

    winrt::init_apartment();

    // Register all certified EPs, or try to register all individually if that
    // fails
    auto catalog = winml::ExecutionProviderCatalog::GetDefault();
    try {
      logger(cil::LogLevel::kInfo,
             "Initializing and registering certified EPs.");
      catalog.EnsureAndRegisterCertifiedAsync().get();
    } catch (const std::exception& e) {
      logger(cil::LogLevel::kWarning,
             std::string("EP catalog init failed: ") + e.what());

      logger(cil::LogLevel::kInfo,
             "Attempting to individually ensure and register all EPs.");

      for (auto const& p : catalog.FindAllProviders()) {
        try {
          if (p.ReadyState() ==
              winml::ExecutionProviderReadyState::NotPresent) {
            p.EnsureReadyAsync().get();
          }
          p.TryRegister();
        } catch (const std::exception& e) {
          logger(cil::LogLevel::kWarning,
                 std::string("EP registration failed: ") + e.what());
        }
      }

      catalog.RegisterCertifiedAsync().get();
    }

    bool foundReadyEP = false;

    for (auto const& p : catalog.FindAllProviders()) {
      if (p.ReadyState() != winml::ExecutionProviderReadyState::Ready) continue;

      std::wstring name = p.Name().c_str();
      std::wstring path = p.LibraryPath().c_str();
      if (name.empty() || path.empty()) continue;
      try {
        auto utf8_name = ConvertWideToUtf8(name);
        auto utf8_path = ConvertWideToUtf8(path);
        foundReadyEP = true;
        logger(cil::LogLevel::kInfo,
               "Registering EP: " + utf8_name + " at " + utf8_path);
        env.RegisterExecutionProviderLibrary(utf8_name.c_str(), path);
      } catch (const std::exception& e) {
        logger(cil::LogLevel::kWarning,
               std::string("EP registration failed: ") + e.what());
      }
    }

    LogInstalledWinMLEPVersions(logger);

    MddBootstrapShutdown();

    initialized = initialized && foundReadyEP;
  });

  return initialized;
}

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
    Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "OGA");

    if (EnsureAndRegisterEPs(env, logger_))
      logger_(LogLevel::kInfo, "Execution Providers are ready.");
    else {
      logger_(LogLevel::kError, "Failed to initialize Execution Providers.");
      SetErrorMessage(
          "WindowsML failed to initialize.\n"
          "Enable Windows Update so the benchmark can download/install WinML.\n"
          "Minimum required: Windows 11 Enterprise 24H2 (OS build 26100.6901)\n"
          "Experience Pack: Windows Feature Experience Pack 1000.26100.253.0\n"
          "Install all pending Windows Updates and reboot if needed.\n"
          "Also install: Microsoft Visual C++ Redistributable (x64) 14.44.53211.");
      return;
    }

    auto allProviders =
        winml::ExecutionProviderCatalog::GetDefault().FindAllProviders();

    std::vector<std::pair<std::wstring, std::wstring>> execution_provider_paths;
    execution_provider_paths.reserve(allProviders.size());
    for (auto const& p : allProviders) {
      if (p.ReadyState() == winml::ExecutionProviderReadyState::Ready) {
        std::wstring name = p.Name().c_str();
        std::wstring path = p.LibraryPath().c_str();
        if (!name.empty() && !path.empty()) {
          execution_provider_paths.emplace_back(std::move(name),
                                                std::move(path));
        }
      }
    }

    DetectWindowsMLDevices(env, execution_provider_paths);
    if (!winml_devices_.has_value() || winml_devices_.value().empty()) {
      SetErrorMessage(
          "No WindowsML devices found. Please ensure you have the required "
          "execution providers installed and registered.");
    }
  }
}

BaseInference::~BaseInference() {
  // Intentionally not calling winrt::uninit_apartment() to keep apartment
  // alive across multiple BaseInference lifetimes and avoid teardown races.
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
      {"CPUExecutionProvider", "CPU"},
      {"OpenVINOExecutionProvider", "OpenVINO"},
      {"QNNExecutionProvider", "QNN"},
      {"VitisAIExecutionProvider", "VitisAI"},
      {"NvTensorRTRTXExecutionProvider", "NvTensorRtRtx"},
      {"DmlExecutionProvider", "DirectML"}};

  static const std::set<std::string> supported_eps = {
      "CPU", "DirectML", "OpenVINO", "QNN", "VitisAI", "NvTensorRtRtx"};

  std::vector<Ort::ConstEpDevice> ep_devices = env.GetEpDevices();

  std::unordered_map<std::string, std::vector<Ort::ConstEpDevice>>
      ep_device_map;
  for (const auto& device : ep_devices) {
    ep_device_map[device.EpName()].push_back(device);
  }

  static const std::vector<std::string> device_types = {"CPU", "GPU", "NPU"};

  struct DirectMLDevice {
    std::string name;
    std::string type;
    std::string perf;
    uint32_t id;
  };

  std::vector<DirectMLDevice> directml_devices;

  std::vector<WinMLDevice> winml_devices;

  for (const auto& [ep_name, devices] : ep_device_map) {
    auto ep = ep_aliases.contains(ep_name) ? ep_aliases.at(ep_name) : ep_name;

    LOGGER_(cil::LogLevel::kInfo, "Execution Provider: " + ep);

    for (const auto& device : devices) {
      auto type = device.Device().Type() >= 0 &&
                          device.Device().Type() < device_types.size()
                      ? device_types[device.Device().Type()]
                      : "Unknown";

      LOGGER_(cil::LogLevel::kInfo,
              " | Vendor: " + std::string(device.EpVendor()) +
                  " | Device Type: " + type +
                  " | DeviceId: " + std::to_string(device.Device().DeviceId()));

#if SHOW_DEVICE_METADATA || SHOW_DEBUG_ENUMERATION || 0
      for (const auto& metadata :
           device.Device().Metadata().GetKeyValuePairs()) {
        LOGGER_(cil::LogLevel::kInfo,
                "            | Metadata: " + metadata.first + " = " +
                    metadata.second);
      }
#endif

      if (type == "unknown" ||
          !device_type_.empty() && (type.empty() || type != device_type_))
        continue;

      if (!supported_eps.contains(ep)) continue;

      if (!device_ep.empty() && device_ep != lower_case(ep)) continue;

      if (ep == "DirectML") {
        try {
          DirectMLDevice directml_device;

          directml_device.name =
              device.Device().Metadata().GetValue("Description");
          std::string perf =
              device.Device().Metadata().GetValue("DxgiHighPerformanceIndex");
          directml_device.perf = std::stoi(perf);
          directml_device.type = type;
          directml_device.id = device.Device().DeviceId();

          directml_devices.emplace_back(directml_device);
        } catch (const std::exception& e) {
          LOGGER_(cil::LogLevel::kWarning,
                  "Failed to parse DML device: " + std::string(e.what()));
        }

      } else {
        auto device_name =
            device.Device().Metadata().GetKeyValuePairs().contains(
                "Description")
                ? device.Device().Metadata().GetValue("Description")
                : std::string(device.Device().Vendor());
        if (device_name.empty()) device_name = std::string(device.EpVendor());

        winml_devices.emplace_back(WinMLDevice{
            ep, type, device_name, device.Device().DeviceId(), "", std::nullopt,
            device.Device().Metadata().GetKeyValuePairs().contains("Discrete")
                ? std::optional<bool>{device.Device().Metadata().GetValue(
                                          "Discrete") == "1"}
                : std::nullopt});
      }
    }
  }

  LOGGER_(cil::LogLevel::kInfo, "Execution Providers and their paths:");
  for (auto& [wname, wpath] : execution_provider_paths) {
    auto ep = ConvertWideToUtf8(wname);
    if (ep_aliases.contains(ep)) {
      ep = ep_aliases.at(ep);
    }

    auto path = ConvertWideToUtf8(wpath);

    LOGGER_(cil::LogLevel::kInfo, "   " + ep + " | " + path);

    // Workaround when EP does not report devices, but was registered.
    auto it = std::find_if(winml_devices.begin(), winml_devices.end(),
                           [&ep](const WinMLDevice& d) { return d.ep == ep; });

    if (it != winml_devices.end()) continue;

    if (!device_ep.empty() && device_ep != lower_case(ep)) continue;

    if (ep == "NvTensorRtRtx") {
      if (device_type_.empty() || device_type_ == "GPU")
        winml_devices.emplace_back(WinMLDevice{ep, "GPU", "Nvidia", 1});
    } else if (ep == "OpenVINO") {
      if (device_type_.empty() || device_type_ == "NPU")
        winml_devices.emplace_back(WinMLDevice{ep, "NPU", "Intel", 0});
      if (device_type_.empty() || device_type_ == "GPU")
        winml_devices.emplace_back(WinMLDevice{ep, "GPU", "Intel", 1});
    } else if (ep == "QNN") {
      if (device_type_.empty() || device_type_ == "NPU")
        winml_devices.emplace_back(WinMLDevice{ep, "NPU", "Qualcomm", 0});
      if (device_type_.empty() || device_type_ == "GPU")
        winml_devices.emplace_back(WinMLDevice{ep, "GPU", "Qualcomm", 1});
    }
  }

  // sort directml devices by performance
  std::sort(directml_devices.begin(), directml_devices.end(),
            [](const DirectMLDevice& a, const DirectMLDevice& b) {
              return a.perf < b.perf;  // Higher performance first
            });

  for (const auto& dml_device : directml_devices) {
    winml_devices.emplace_back(WinMLDevice{"DirectML", dml_device.type,
                                           dml_device.name, dml_device.id});
  }

  // Remove duplicates
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

  if (!winml_devices.empty() && directml_devices.size()) {
    cil::common::DirectML::DeviceEnumeration directml_device_enumeration;

    // Enumerate devices using CLI
    auto directml_devices_enumerated =
        directml_device_enumeration.EnumerateDevices(deps_dir_, device_type_,
                                                     device_vendor);

    bool has_directml = false;
    for (auto it = winml_devices.begin(); it != winml_devices.end();) {
      auto& device = *it;
      if (device.ep != "DirectML") {
        ++it;
        continue;
      }

      if (directml_devices_enumerated.empty()) {
        LOGGER_(LogLevel::kError,
                "No DirectML devices found for: " + device.name);
        it = winml_devices.erase(it);
        continue;
      }

      auto directml_device_it = std::find_if(
          directml_devices_enumerated.begin(),
          directml_devices_enumerated.end(),
          [&device](
              const cil::common::DirectML::DeviceEnumeration::DeviceInfo& d) {
            return d.name == device.name;
          });
      if (directml_device_it == directml_devices_enumerated.end()) {
        LOGGER_(LogLevel::kWarning, "DirectML LUID not found: " + device.name);
        it = winml_devices.erase(it);
        continue;
      }
      // Assign the DirectML device entry
      device.directml_device_entry = (int)std::distance(
          directml_devices_enumerated.begin(), directml_device_it);
      ++it;

      has_directml = true;
    }

    if (has_directml)
      directml_devices_ = std::move(directml_devices_enumerated);
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
  available_providers.insert("NvTensorRtRtx");

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
                available_providers.insert("NvTensorRtRtx");
                execution_providers.insert("NvTensorRtRtx");
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
