#include "api_handler.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "dylib.hpp"
#include "version.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#define MAX_DEVICE_TYPE_LENGTH 16

namespace cil {

API_Handler::API_Handler(const std::string& library_path, Logger logger,
                         bool force_unload_ep)
    : library_path_(library_path),
      logger_(logger),
      force_unload_ep_(force_unload_ep) {
  if (library_path_.empty()) {
    logger(LogLevel::kFatal, "No IHV library path provided.");
    return;
  }

  library_path_directory_ =
      fs::absolute(fs::path(library_path_).parent_path()).string();


#ifdef _WIN32
  // On Windows, temporarily set DLL directory to ensure this EP's dependencies
  // are loaded from its own directory, preventing conflicts with other EPs
  WCHAR previousDllDirectory[MAX_PATH] = {0};
  GetDllDirectoryW(MAX_PATH, previousDllDirectory);
  SetDllDirectoryW(
      std::filesystem::path(library_path_directory_).wstring().c_str());
#endif

  library_path_handle_ = utils::AddLibraryPath(library_path_directory_);
  try {
    library_ = std::make_unique<dylib>(library_path_.c_str(),
                                       dylib::no_filename_decorations);
    setup_ = library_->get_function<std::remove_pointer_t<API_IHV_Setup_func>>(
        "API_IHV_Setup");
    enumerate_devices_ = library_->get_function<
        std::remove_pointer_t<API_IHV_EnumerateDevices_func>>(
        "API_IHV_EnumerateDevices");
    init_ = library_->get_function<std::remove_pointer_t<API_IHV_Init_func>>(
        "API_IHV_Init");
    prepare_ =
        library_->get_function<std::remove_pointer_t<API_IHV_Prepare_func>>(
            "API_IHV_Prepare");
    infer_ = library_->get_function<std::remove_pointer_t<API_IHV_Infer_func>>(
        "API_IHV_Infer");
    reset_ = library_->get_function<std::remove_pointer_t<API_IHV_Reset_func>>(
        "API_IHV_Reset");
    deinit_ =
        library_->get_function<std::remove_pointer_t<API_IHV_Deinit_func>>(
            "API_IHV_Deinit");
    release_ =
        library_->get_function<std::remove_pointer_t<API_IHV_Release_func>>(
            "API_IHV_Release");
  } catch (std::exception& ex) {
    library_.reset();  // in case it was loaded
    logger(LogLevel::kFatal, ex.what());
  }

#ifdef _WIN32
  // Restore previous DLL directory
  SetDllDirectoryW(previousDllDirectory[0] != 0 ? previousDllDirectory
                                                : nullptr);
#endif
}

API_Handler::~API_Handler() {
  if (ihv_struct_ != nullptr) Release();

  try {
    library_.reset();
    if (library_path_handle_.IsValid()) {
      utils::RemoveLibraryPath(library_path_handle_);
    }
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to free IHV library.");
  }

#ifdef WIN32
  if (!force_unload_ep_) {
    return;
  }

  // Unload execution provider resources to prevent DLL conflicts when switching
  // EPs
  logger_(LogLevel::kInfo, std::string("Unloading EP resources for: ") +
                               library_path_directory_);
  // On Windows, unload all DLLs from the EP's directory
  // to prevent conflicts when switching to a different EP
  logger_(LogLevel::kInfo,
          std::string("Attempting to force unload DLLs from: ") +
              library_path_directory_);

  // First, collect all modules from this EP's directory
  std::vector<std::wstring> modulesToUnload;
  HMODULE hMods[1024];
  DWORD cbNeeded;
  HANDLE hProcess = GetCurrentProcess();

  if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
    unsigned int totalModules = cbNeeded / sizeof(HMODULE);
    logger_(LogLevel::kInfo, std::string("Found ") +
                                 std::to_string(totalModules) +
                                 std::string(" total loaded modules"));

    for (unsigned int i = 0; i < totalModules; i++) {
      WCHAR szModPath[MAX_PATH];
      if (GetModuleFileNameExW(hProcess, hMods[i], szModPath,
                               sizeof(szModPath) / sizeof(WCHAR))) {
        std::wstring modPath(szModPath);
        // Normalize both paths to use consistent separators
        std::filesystem::path depsPathNormalized =
            std::filesystem::path(library_path_directory_);
        std::filesystem::path modPathNormalized =
            std::filesystem::path(modPath);

        std::wstring depsPathWstr = depsPathNormalized.wstring();
        std::wstring modPathWstr = modPathNormalized.wstring();

        // Log all DLL paths containing "MLPerf" or "IHV" for debugging
        std::string modPathStr = modPathNormalized.string();
        if (modPathStr.find("MLPerf") != std::string::npos ||
            modPathStr.find("IHV") != std::string::npos ||
            modPathStr.find("openvino") != std::string::npos ||
            modPathStr.find("onnxruntime") != std::string::npos) {
          logger_(LogLevel::kInfo,
                  std::string("Checking module: ") + modPathStr);
        }

        // Check if this module is from our EP's directory (normalized path
        // comparison)
        if (modPathWstr.find(depsPathWstr) != std::wstring::npos) {
          logger_(LogLevel::kInfo,
                  std::string("MATCH - Will unload: ") + modPathStr);
          modulesToUnload.push_back(modPath);
        }
        // Also check for WindowsAppRuntime OpenVINO EP DLLs that might conflict
        else if (modPathStr.find("WindowsApps") != std::string::npos &&
                 modPathStr.find("OpenVINO") != std::string::npos) {
          logger_(LogLevel::kInfo,
                  std::string(
                      "MATCH (WindowsApp OpenVINO) - Will attempt unload: ") +
                      modPathStr);
          modulesToUnload.push_back(modPath);
        }
      }
    }

    logger_(LogLevel::kInfo,
            std::string("Found ") + std::to_string(modulesToUnload.size()) +
                std::string(" modules to unload from EP directory"));

    // Now try to unload each module aggressively
    for (const auto& modPath : modulesToUnload) {
      std::string modPathStr = std::filesystem::path(modPath).string();
      logger_(LogLevel::kInfo,
              std::string("Attempting to unload: ") + modPathStr);

      // Get a fresh handle to the module
      HMODULE hMod = GetModuleHandleW(modPath.c_str());
      if (hMod != nullptr) {
        // Try to free the library many times (Windows reference counts DLL
        // loads)
        int unloadAttempts = 0;
        const int MAX_UNLOAD_ATTEMPTS = 1000;  // Increased limit

        while (unloadAttempts < MAX_UNLOAD_ATTEMPTS) {
          if (!FreeLibrary(hMod)) {
            // FreeLibrary failed, module might be unloaded or error occurred
            break;
          }
          unloadAttempts++;

          // Check if module is still loaded
          if (GetModuleHandleW(modPath.c_str()) == nullptr) {
            logger_(LogLevel::kInfo,
                    std::string("Successfully unloaded after ") +
                        std::to_string(unloadAttempts) +
                        std::string(" attempts: ") + modPathStr);
            break;
          }
        }

        if (unloadAttempts >= MAX_UNLOAD_ATTEMPTS) {
          logger_(LogLevel::kWarning,
                  std::string("Module still loaded after ") +
                      std::to_string(unloadAttempts) +
                      std::string(" attempts: ") + modPathStr);
        }
      } else {
        logger_(LogLevel::kInfo,
                std::string("Module already unloaded: ") + modPathStr);
      }
    }

    logger_(LogLevel::kInfo, std::string("Completed DLL unload attempts"));
  } else {
    logger_(LogLevel::kWarning,
            std::string("Failed to enumerate process modules"));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
}

bool API_Handler::IsLoaded() const { return library_.get() != nullptr; }

bool API_Handler::Setup(const std::string& ep_name,
                        const std::string& model_name,
                        const std::string& model_path,
                        const std::string& deps_dir,
                        const nlohmann::json& ep_settings,
                        std::string& device_type_out) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  std::string ep_settings_str = ep_settings.dump();

  API_IHV_Setup_t api = {API_IHV_VERSION,
                         this,
                         &Log,
                         APP_VERSION_STRING,
                         ep_name.c_str(),
                         model_name.c_str(),
                         model_path.c_str(),
                         deps_dir.c_str(),
                         ep_settings_str.c_str()};

  try {
    ihv_struct_ = setup_(&api);

    if (!ihv_struct_ || ihv_struct_->device_type == nullptr ||
        strnlen(ihv_struct_->device_type, MAX_DEVICE_TYPE_LENGTH) == 0) {
      logger_(LogLevel::kFatal, "Failed to setup IHV library. Invalid device type.");
      return false;
    }

    device_type_out = ihv_struct_->device_type;

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to setup IHV library.");
    return false;
  }
}

bool API_Handler::EnumerateDevices(API_Handler::DeviceListPtr& device_list) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  API_IHV_DeviceEnumeration_t api = {this, &Log, ihv_struct_, nullptr};

  try {
    if (enumerate_devices_(&api) != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kFatal,
              "IHV unsuccessful device enumeration.");
      return false;
    }
    if (nullptr == api.device_list) {
      logger_(LogLevel::kFatal, "IHV no device list provided.");
      return false;
    }

    device_list = api.device_list;
    return true;
  } catch (const std::exception& e) {
    logger_(LogLevel::kFatal, std::string("Failed to call EnumerateDevices() for IHV library. ") + e.what());
    return false;
  }
}

bool API_Handler::Init(const std::string& model_config,
                       std::optional<API_IHV_DeviceID_t> device_id) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  API_IHV_Init_t api = {this, &Log, ihv_struct_, model_config.c_str(),
                        device_id.has_value() ? &device_id.value() : nullptr};

  try {
    return init_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to initialize IHV library.");
    return false;
  }
}

bool API_Handler::Infer(API_IHV_IO_Data_t& io_data) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  API_IHV_Infer_t api = {this, &Log, ihv_struct_, &io_data};

  try {
    int ret = infer_(&api);

    if (ret != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kError,
                  "IHV library inference returned error: " + std::to_string(ret));
      return false;
    }

    if ((API_IHV_Callback_Type::API_IHV_CB_None ==
         api.io_data->callback.type) &&
        (api.io_data->output_size == 0 || api.io_data->output == nullptr)) {
      logger_(LogLevel::kError,
              "IHV library inference returned no output data.");
      return false;
    }

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to call Infer() for IHV library.");
    return false;
  }
}

bool API_Handler::Deinit() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  API_IHV_Deinit_t api = {this, &Log, ihv_struct_};

  try {
    return deinit_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to deinitialize IHV library.");
    return false;
  }
}

bool API_Handler::Release() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  if (ihv_struct_ == nullptr) {
    logger_(LogLevel::kFatal, "Invalid IHV library struct.");
    return false;
  }

  API_IHV_Release_t api = {this, &Log, ihv_struct_};

  try {
    release_(&api);

    ihv_struct_ = nullptr;

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to release IHV library.");
    return false;
  }
}

bool API_Handler::Prepare() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return prepare_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to call Prepare() for IHV library.");
    return false;
  }
}

bool API_Handler::Reset() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return reset_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to call Reset() for IHV library.");
    return false;
  }
}

std::string API_Handler::CanBeLoaded(const std::string& library_path) {
  std::stringstream ss;

  Logger logger = [&ss](LogLevel level, std::string message) {
    using enum LogLevel;

    switch (level) {
      case kInfo:
        ss << "INFO: ";
        break;
      case kWarning:
        ss << "WARNING: ";
        break;
      case kError:
        ss << "ERROR: ";
        break;
      case kFatal:
        ss << "FATAL: ";
        break;
    }
    ss << message;
  };

  auto api_handler = std::make_unique<API_Handler>(library_path, logger, false);

  return ss.str();
}

API_Handler::DeviceList API_Handler::EnumerateDevices(
    const std::string& library_path, const std::string& ep_name,
    const std::string& model_name, const nlohmann::json& ep_settings,
    std::string& error, std::string& log) {
  std::stringstream ss;
  std::stringstream ss_err;

  Logger logger = [&ss, &ss_err](LogLevel level, std::string message) {
    using enum LogLevel;

    switch (level) {
      case kInfo:
        ss << "INFO: ";
        ss << message << std::endl;
        break;
      case kWarning:
        ss << "WARNING: ";
        ss << message << std::endl;
        break;
      case kError:
        ss_err << "ERROR: ";
        ss_err << message << std::endl;
        break;
      case kFatal:
        ss_err << "FATAL: ";
        ss_err << message << std::endl;
        break;
    }
  };

  auto api_handler = std::make_unique<API_Handler>(library_path, logger, false);

  auto deps_dir = fs::absolute(fs::path(library_path).parent_path()).string();

  std::string device_type_out;
  if (!api_handler->Setup(ep_name, model_name, "", deps_dir, ep_settings,
                          device_type_out)) {
    log = ss.str();
    error = ss_err.str();
    return {};
  }

  API_Handler::DeviceListPtr device_list;

  if (!api_handler->EnumerateDevices(device_list)) {
    log = ss.str();
    error = ss_err.str();
    return {};
  }

  DeviceList devices;
  for (size_t i = 0; i < device_list->count; ++i) {
    auto device_id = device_list->device_info_data[i].device_id;
    auto device_name = device_list->device_info_data[i].device_name;
    if (device_name == nullptr) {
      device_name = "";
    }
    DeviceInfo device_info = {device_id, device_name};
    devices.emplace_back(device_info);
  }

  log = ss.str();
  error = ss_err.str();

  return devices;
}

void API_Handler::Log(void* context, API_IHV_LogLevel level,
                      const char* message) {
  auto* obj = static_cast<API_Handler*>(context);

  if (obj == nullptr) {
    throw std::runtime_error("API_Handler object is null.");
  }

  switch (level) {
    case API_IHV_INFO:
      obj->logger_(LogLevel::kInfo, message);
      break;
    case API_IHV_WARNING:
      obj->logger_(LogLevel::kWarning, message);
      break;
    case API_IHV_ERROR:
      obj->logger_(LogLevel::kError, message);
      obj->ihv_errors_ += message;
      obj->ihv_errors_ += "\n";
      break;
    case API_IHV_FATAL:
    default:
      obj->logger_(LogLevel::kFatal, message);
      obj->ihv_errors_ += message;
      obj->ihv_errors_ += "\n";
      break;
  }
}

}  // namespace cil
