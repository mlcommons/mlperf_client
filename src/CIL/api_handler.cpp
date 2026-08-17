#include "api_handler.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <span>
#include <sstream>
#include <thread>

#include "dylib.hpp"
#include "version.h"
#include "api_handler_ipc.h"

#ifdef _WIN32
#include <psapi.h>
#include <windows.h>
#endif

#ifdef __APPLE__
#include <pthread/qos.h>

namespace {
__attribute__((constructor)) void mlperf_raise_main_thread_qos() {
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
}
}  // namespace
#endif

#define MAX_DEVICE_TYPE_LENGTH 16

namespace cil {

#if IHV_SUBPROCESS


bool API_Handler::s_default_subprocess_mode_ = true;

void API_Handler::SetDefaultSubprocessMode(bool enabled) {
  s_default_subprocess_mode_ = enabled;
}

bool API_Handler::GetDefaultSubprocessMode() {
  return s_default_subprocess_mode_;
}

namespace {

std::map<API_IHV_Callback_Type, API_Handler::CallbackAdapter::Factory>&
CallbackAdapterFactories() {
  static std::map<API_IHV_Callback_Type, API_Handler::CallbackAdapter::Factory>
      factories;
  return factories;
}

}  // namespace

void API_Handler::CallbackAdapter::Register(API_IHV_Callback_Type callback_type,
                                            Factory factory) {
  CallbackAdapterFactories()[callback_type] = std::move(factory);
}

std::unique_ptr<API_Handler::CallbackAdapter>
API_Handler::CallbackAdapter::Create(API_IHV_Callback_Type callback_type) {
  auto it = CallbackAdapterFactories().find(callback_type);
  if (it == CallbackAdapterFactories().end() || !it->second) {
    return nullptr;
  }
  return it->second();
}

const nlohmann::json& API_Handler::GetCallbackAdapterData() const {
  return callback_adapter_data_;
}

int API_Handler::RunSubprocessClient(const std::string& token) {
  utils::SetCurrentDirectory(utils::GetCurrentDirectory());
  return API_Handler::Client::Run(token);
}

#endif  // IHV_SUBPROCESS

API_Handler::API_Handler(const std::string& library_path, Logger logger,
                         bool force_unload_ep
#if IHV_SUBPROCESS
                         ,
                         std::optional<bool> use_subprocess
#endif
                         )
    : library_path_(library_path),
      logger_(logger),
      force_unload_ep_(force_unload_ep)
#if IHV_SUBPROCESS
      ,
      use_subprocess_(use_subprocess.value_or(s_default_subprocess_mode_))
#endif
{
  if (library_path_.empty()) {
    logger(LogLevel::kFatal, "No IHV library path provided.");
    return;
  }

  library_path_directory_ =
      fs::absolute(fs::path(library_path_).parent_path()).string();

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    subprocess_server_ = std::make_unique<Server>(logger_, ihv_errors_);

    if (!subprocess_server_->Start()) {
      logger_(LogLevel::kFatal, "Failed to start IHV subprocess");
      subprocess_server_.reset();
    }
    return;
  }
#endif

#ifdef _WIN32
  WCHAR previousDllDirectory[MAX_PATH] = {0};
  GetDllDirectoryW(MAX_PATH, previousDllDirectory);
  SetDllDirectoryW(
      std::filesystem::path(library_path_directory_).wstring().c_str());
#endif

  library_path_handle_ = utils::AddLibraryPath(library_path_directory_);
  try {
    library_ = std::make_unique<dylib>(library_path_.c_str(),
                                       dylib::no_filename_decorations);
    if (library_->has_symbol("API_IHV_GetAPIVersion")) {
      get_api_version_ =
          library_
              ->get_function<std::remove_pointer_t<API_IHV_GetAPIVersion_func>>(
                  "API_IHV_GetAPIVersion");
      if (const unsigned reported_version = get_api_version_();
          reported_version != API_IHV_VERSION) {
        logger_(LogLevel::kWarning,
                "API version mismatch ( " + std::to_string(reported_version) +
                    " != " + std::to_string(API_IHV_VERSION) + " )");
      }
    } else {
      logger_(LogLevel::kWarning, "API version missing");
    }
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
  SetDllDirectoryW(previousDllDirectory[0] != 0 ? previousDllDirectory
                                                : nullptr);
#endif
}

API_Handler::~API_Handler() {
#if IHV_SUBPROCESS
  if (use_subprocess_) {
    if (subprocess_server_) {
      subprocess_server_->Stop();
      subprocess_server_.reset();
    }
    return;
  }
#endif

  try {
    if (ihv_struct_ != nullptr) Release();

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

  logger_(LogLevel::kInfo, std::string("Unloading EP resources for: ") +
                               library_path_directory_);
  logger_(LogLevel::kInfo,
          std::string("Attempting to force unload DLLs from: ") +
              library_path_directory_);

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
        std::filesystem::path depsPathNormalized =
            std::filesystem::path(library_path_directory_);
        std::filesystem::path modPathNormalized =
            std::filesystem::path(modPath);

        std::wstring depsPathWstr = depsPathNormalized.wstring();
        std::wstring modPathWstr = modPathNormalized.wstring();

        std::string modPathStr = modPathNormalized.string();
        if (modPathStr.find("MLPerf") != std::string::npos ||
            modPathStr.find("IHV") != std::string::npos ||
            modPathStr.find("openvino") != std::string::npos ||
            modPathStr.find("onnxruntime") != std::string::npos) {
          logger_(LogLevel::kInfo,
                  std::string("Checking module: ") + modPathStr);
        }

        if (modPathWstr.find(depsPathWstr) != std::wstring::npos) {
          logger_(LogLevel::kInfo,
                  std::string("MATCH - Will unload: ") + modPathStr);
          modulesToUnload.push_back(modPath);
        } else if (modPathStr.find("WindowsApps") != std::string::npos &&
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

    for (const auto& modPath : modulesToUnload) {
      std::string modPathStr = std::filesystem::path(modPath).string();
      logger_(LogLevel::kInfo,
              std::string("Attempting to unload: ") + modPathStr);

      HMODULE hMod = GetModuleHandleW(modPath.c_str());
      if (hMod != nullptr) {
        int unloadAttempts = 0;
        const int MAX_UNLOAD_ATTEMPTS = 1000;

        while (unloadAttempts < MAX_UNLOAD_ATTEMPTS) {
          if (!FreeLibrary(hMod)) {
            break;
          }
          unloadAttempts++;

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

bool API_Handler::IsLoaded() const {
#if IHV_SUBPROCESS
  if (use_subprocess_) {
    return subprocess_server_ && subprocess_server_->IsRunning();
  }
#endif
  return library_.get() != nullptr;
}

bool API_Handler::Setup(const std::string& ep_name,
                        const std::string& scenario_name,
                        const std::string& model_base_name,
                        const std::string& model_path,
                        const std::string& deps_dir,
                        const nlohmann::json& ep_settings,
                        std::string& device_type_out) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    nlohmann::json payload = {
        {"library_path", library_path_},  {"ep_name", ep_name},
        {"scenario_name", scenario_name}, {"model_base_name", model_base_name},
        {"model_path", model_path},       {"deps_dir", deps_dir},
        {"ep_settings", ep_settings}};

    auto response = subprocess_server_->SendMessage(IPC::MessageType::kSetup,
                                                    payload, kIPCTimeoutLong);
    if (!subprocess_server_->HandleResponse(response, "setup",
                                            LogLevel::kFatal)) {
      return false;
    }
    device_type_out = response.value("device_type", "Unknown");
    return true;
  }
#endif

  std::string ep_settings_str = ep_settings.dump();

  API_IHV_Setup_t api = {API_IHV_VERSION,
                         this,
                         &Log,
                         APP_VERSION_STRING,
                         ep_name.c_str(),
                         scenario_name.c_str(),
                         model_path.c_str(),
                         deps_dir.c_str(),
                         ep_settings_str.c_str(),
                         model_base_name.c_str()};

  try {
    ihv_struct_ = setup_(&api);

    if (!ihv_struct_ || ihv_struct_->device_type == nullptr ||
        strnlen(ihv_struct_->device_type, MAX_DEVICE_TYPE_LENGTH) == 0) {
      logger_(LogLevel::kFatal,
              "Failed to setup IHV library. Invalid device type.");
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

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(
        IPC::MessageType::kEnumerateDevices, {}, kIPCTimeoutLong);
    if (!subprocess_server_->HandleResponse(response, "device enumeration",
                                            LogLevel::kFatal)) {
      return false;
    }

    auto devices = response.value("devices", nlohmann::json::array());
    size_t device_count = devices.size();

    size_t alloc_size = sizeof(API_IHV_DeviceList_t) +
                        device_count * sizeof(API_IHV_DeviceInfo_t);
    subprocess_server_->device_list_storage_.resize(alloc_size);

    auto* list = reinterpret_cast<API_IHV_DeviceList_t*>(
        subprocess_server_->device_list_storage_.data());

    list->count = device_count;
    for (size_t i = 0; i < device_count; ++i) {
      list->device_info_data[i].device_id = devices[i].value("device_id", 0);
      std::string name = devices[i].value("device_name", "");
      auto& device_name = list->device_info_data[i].device_name;
      size_t copied = name.copy(device_name, sizeof(device_name) - 1);
      device_name[copied] = '\0';
    }

    device_list = list;
    return true;
  }
#endif

  API_IHV_DeviceEnumeration_t api = {this, &Log, ihv_struct_, nullptr};

  try {
    if (enumerate_devices_(&api) != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kFatal, "IHV unsuccessful device enumeration.");
      return false;
    }
    if (nullptr == api.device_list) {
      logger_(LogLevel::kFatal, "IHV no device list provided.");
      return false;
    }

    device_list = api.device_list;
    return true;
  } catch (const std::exception& e) {
    logger_(LogLevel::kFatal,
            std::string("Failed to call EnumerateDevices() for IHV library. ") +
                e.what());
    return false;
  }
}

bool API_Handler::Init(const std::string& model_config,
                       std::optional<API_IHV_DeviceID_t> device_id) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    nlohmann::json payload = {{"model_config", model_config}};
    if (device_id.has_value()) {
      payload["device_id"] = device_id.value();
    }

    auto response = subprocess_server_->SendMessage(IPC::MessageType::kInit,
                                                    payload, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "init",
                                              LogLevel::kFatal);
  }
#endif

  API_IHV_Init_t api = {this, &Log, ihv_struct_, model_config.c_str(),
                        device_id.has_value() ? &device_id.value() : nullptr};

  try {
    return init_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to initialize IHV library.");
    return false;
  }
}

bool API_Handler::Infer(API_IHV_IO_Data_t& io_data) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    const bool is_image = io_data.callback.type == API_IHV_CB_ImageStep;

    if (io_data.callback.type != API_IHV_CB_None) {
      nlohmann::json adapter_payload = {
          {"callback_type", static_cast<int>(io_data.callback.type)}};

      if (is_image) {
        adapter_payload["prompt"] =
            io_data.input
                ? std::string(static_cast<const char*>(io_data.input),
                              io_data.input_size)
                : std::string();
      } else {
        std::vector<uint32_t> adapter_input;
        if (io_data.input && io_data.input_size > 0) {
          const auto input_ptr = static_cast<const uint32_t*>(io_data.input);
          adapter_input.assign(input_ptr, input_ptr + io_data.input_size);
        }
        adapter_payload["input"] = adapter_input;
      }

      callback_adapter_data_ = nlohmann::json();
      auto adapter_response = subprocess_server_->SendInferCommand(
          adapter_payload, nullptr, nullptr);
      if (!subprocess_server_->HandleResponse(adapter_response, "inference")) {
        return false;
      }
      if (adapter_response.contains("adapter_data")) {
        callback_adapter_data_ = adapter_response["adapter_data"];
      }
      if (adapter_response.contains("output_bytes")) {
        subprocess_server_->infer_output_bytes_ =
            adapter_response["output_bytes"].get<std::vector<uint8_t>>();
        io_data.output = subprocess_server_->infer_output_bytes_.data();
        io_data.output_size = static_cast<unsigned>(
            subprocess_server_->infer_output_bytes_.size());
      }
      return true;
    }

    nlohmann::json payload;
    if (is_image) {
      std::string prompt(static_cast<const char*>(io_data.input),
                         io_data.input_size);
      payload = {{"callback_type", static_cast<int>(io_data.callback.type)},
                 {"prompt", prompt}};
    } else {
      std::vector<uint32_t> input_tokens;
      if (io_data.input && io_data.input_size > 0) {
        const auto input_ptr = static_cast<const uint32_t*>(io_data.input);
        input_tokens.assign(input_ptr, input_ptr + io_data.input_size);
      }
      payload = {{"callback_type", static_cast<int>(io_data.callback.type)},
                 {"input", input_tokens}};
    }

    Server::TokenCallback token_callback = nullptr;
    Server::ImageStepCallback image_step_callback = nullptr;

    if (io_data.callback.function != nullptr &&
        io_data.callback.object != nullptr) {
      if (is_image) {
        auto original_callback =
            reinterpret_cast<API_IHV_ImageStep_Callback_func>(
                io_data.callback.function);
        void* original_object = io_data.callback.object;
        image_step_callback = [original_callback, original_object](
                                  uint32_t phase, uint32_t step,
                                  uint32_t total_steps) {
          API_IHV_ImageStep_Event_t event{
              static_cast<API_IHV_ImageStep_Phase>(phase), step, total_steps,
              nullptr, 0.0};
          original_callback(original_object, &event);
        };
      } else {
        auto original_callback = reinterpret_cast<void (*)(void*, uint32_t)>(
            io_data.callback.function);
        void* original_object = io_data.callback.object;
        token_callback = [original_callback, original_object](uint32_t token) {
          original_callback(original_object, token);
        };
      }
    }

    auto response = subprocess_server_->SendInferCommand(
        payload, token_callback, image_step_callback);
    if (!subprocess_server_->HandleResponse(response, "inference")) {
      return false;
    }

    if (is_image) {
      if (response.contains("output_bytes")) {
        subprocess_server_->infer_output_bytes_ =
            response["output_bytes"].get<std::vector<uint8_t>>();
        io_data.output = subprocess_server_->infer_output_bytes_.data();
        io_data.output_size = static_cast<unsigned>(
            subprocess_server_->infer_output_bytes_.size());
      }
    } else {
      if (response.contains("output")) {
        subprocess_server_->infer_output_buffer_ =
            response["output"].get<std::vector<uint32_t>>();
        io_data.output = subprocess_server_->infer_output_buffer_.data();
        io_data.output_size = static_cast<unsigned>(
            subprocess_server_->infer_output_buffer_.size());
      }
    }
    return true;
  }
#endif

  API_IHV_Infer_t api = {this, &Log, ihv_struct_, &io_data};

  try {
    if (int ret = infer_(&api); ret != API_IHV_RETURN_SUCCESS) {
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
    logger_(LogLevel::kFatal, "Failed to call Infer() for IHV library.");
    return false;
  }
}

bool API_Handler::Deinit() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kDeinit,
                                                    {}, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "deinit");
  }
#endif

  API_IHV_Deinit_t api = {this, &Log, ihv_struct_};

  try {
    return deinit_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to deinitialize IHV library.");
    return false;
  }
}

bool API_Handler::Release() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kRelease,
                                                    {}, kIPCTimeoutLong);
    return response.value("success", false);
  }
#endif

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
    logger_(LogLevel::kFatal, "Failed to release IHV library.");
    return false;
  }
}

bool API_Handler::Prepare() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kPrepare,
                                                    {}, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "prepare");
  }
#endif

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return prepare_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to call Prepare() for IHV library.");
    return false;
  }
}

bool API_Handler::Reset() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kReset,
                                                    {}, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "reset");
  }
#endif

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return reset_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to call Reset() for IHV library.");
    return false;
  }
}

std::string API_Handler::CanBeLoaded(const std::string& library_path
#if IHV_SUBPROCESS
                                     ,
                                     std::optional<bool> use_subprocess
#endif
) {
  std::stringstream ss;
  std::string ihv_errors;

  Logger logger = [&ss](LogLevel level, std::string message) {
    if (level == LogLevel::kError || level == LogLevel::kFatal) {
      ss << message;
      // Ignore other log levels, if result is non emtpy it means failure.
    }
  };

#if IHV_SUBPROCESS
  if (use_subprocess.value_or(s_default_subprocess_mode_)) {
    Server server(logger, ihv_errors);
    if (!server.Start()) {
      return "Failed to start subprocess";
    }

    nlohmann::json payload = {{"library_path", library_path}};
    auto response = server.SendMessage(IPC::MessageType::kCanBeLoaded, payload,
                                       kIPCTimeout);

    server.SendMessage(IPC::MessageType::kShutdown, {}, kIPCTimeout);
    server.Stop();

    if (response.value("success", false)) {
      return "";
    }
    return response.value("error", "Failed to load in subprocess");
  }
#endif

  auto api_handler = std::make_unique<API_Handler>(library_path, logger, false
#if IHV_SUBPROCESS
                                                   ,
                                                   false
#endif
  );

  return ss.str();
}

API_Handler::DeviceList API_Handler::EnumerateDevices(
    const std::string& library_path, const std::string& ep_name,
    const std::string& scenario_name, const nlohmann::json& ep_settings,
    std::string& error, std::string& log
#if IHV_SUBPROCESS
    ,
    std::optional<bool> use_subprocess
#endif
) {
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

  auto api_handler = std::make_unique<API_Handler>(library_path, logger, false
#if IHV_SUBPROCESS
                                                   ,
                                                   use_subprocess
#endif
  );

  auto deps_dir = fs::absolute(fs::path(library_path).parent_path()).string();

  if (std::string device_type_out;
      !api_handler->Setup(ep_name, scenario_name, "", "", deps_dir, ep_settings,
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
