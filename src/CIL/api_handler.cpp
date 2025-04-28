#include "api_handler.h"

#include <sstream>

#include "../CLI/version.h"
#include "dylib.hpp"

#define MAX_DEVICE_TYPE_LENGTH 16

namespace cil {

API_Handler::API_Handler(Type type, const std::string& library_path,
                         Logger logger)
    : type_(type), library_path_(library_path), logger_(logger) {
  if (library_path_.empty()) {
    logger(LogLevel::kFatal, "No " + LibraryName() + " library path provided.");
    return;
  }

  library_path_directory_ =
      fs::absolute(fs::path(library_path_).parent_path()).string();
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
}

API_Handler::~API_Handler() {
  if (ihv_struct_ != nullptr) Release();

  try {
    library_.reset();
    if (library_path_handle_.IsValid()) {
      utils::RemoveLibraryPath(library_path_handle_);
    }
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to free " + LibraryName() + " library.");
  }
}

bool API_Handler::IsLoaded() const { return library_.get() != nullptr; }

bool API_Handler::Setup(const std::string& ep_name,
                        const std::string& model_name,
                        const std::string& model_path,
                        const std::string& deps_dir,
                        const nlohmann::json& ep_settings,
                        std::string& device_type_out) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
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
      logger_(LogLevel::kFatal, "Failed to setup " + LibraryName() +
                                    " library. Invalid device type.");
      return false;
    }

    device_type_out = ihv_struct_->device_type;

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to setup " + LibraryName() + " library.");
    return false;
  }
}

bool API_Handler::EnumerateDevices(API_Handler::DeviceListPtr& device_list) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  API_IHV_DeviceEnumeration_t api = {this, &Log, ihv_struct_, nullptr};

  try {
    if (enumerate_devices_(&api) != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kFatal,
              LibraryName() + " unsuccessful device enumeration.");
      return false;
    }
    if (nullptr == api.device_list) {
      logger_(LogLevel::kFatal, LibraryName() + " no device list provided.");
      return false;
    }

    device_list = api.device_list;
    return true;
  } catch (const std::exception& e) {
    logger_(LogLevel::kFatal, "Failed to call EnumerateDevices() for " +
                                  LibraryName() + " library. " + e.what());
    return false;
  }
}

bool API_Handler::Init(const std::string& model_config,
                       std::optional<API_IHV_DeviceID_t> device_id) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  API_IHV_Init_t api = {this, &Log, ihv_struct_, model_config.c_str(),
                        device_id.has_value() ? &device_id.value() : nullptr};

  try {
    return init_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to initialize " + LibraryName() + " library.");
    return false;
  }
}

bool API_Handler::Infer(API_IHV_IO_Data_t& io_data) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  API_IHV_Infer_t api = {this, &Log, ihv_struct_, &io_data};

  try {
    int ret = infer_(&api);

    if (ret != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kError,
              LibraryName() +
                  " library inference returned error: " + std::to_string(ret));
      return false;
    }

    if ((API_IHV_Callback_Type::API_IHV_CB_None ==
         api.io_data->callback.type) &&
        (api.io_data->output_size == 0 || api.io_data->output == nullptr)) {
      logger_(LogLevel::kError,
              LibraryName() + " library inference returned no output data.");
      return false;
    }

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to call Infer() for " + LibraryName() + " library.");
    return false;
  }
}

bool API_Handler::Deinit() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  API_IHV_Deinit_t api = {this, &Log, ihv_struct_};

  try {
    return deinit_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to deinitialize " + LibraryName() + " library.");
    return false;
  }
}

bool API_Handler::Release() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  if (ihv_struct_ == nullptr) {
    logger_(LogLevel::kFatal, "Invalid " + LibraryName() + " library struct.");
    return false;
  }

  API_IHV_Release_t api = {this, &Log, ihv_struct_};

  try {
    release_(&api);

    ihv_struct_ = nullptr;

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to release " + LibraryName() + " library.");
    return false;
  }
}

bool API_Handler::Prepare() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return prepare_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to call Prepare() for " + LibraryName() + " library.");
    return false;
  }
}

bool API_Handler::Reset() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, LibraryName() + " library is not loaded.");
    return false;
  }

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return reset_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal,
            "Failed to call Reset() for " + LibraryName() + " library.");
    return false;
  }
}

std::string API_Handler::LibraryName() const {
  return "IHV";
}

std::string API_Handler::CanBeLoaded(API_Handler::Type type,
                                     const std::string& library_path) {
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

  auto api_handler = std::make_unique<API_Handler>(type, library_path, logger);

  return ss.str();
}

API_Handler::DeviceList API_Handler::EnumerateDevices(
    API_Handler::Type type,
                              const std::string& library_path,
                              const std::string& ep_name,
                              const std::string& model_name,
                              const nlohmann::json& ep_settings,
                              std::string& error,
                              std::string& log) {
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

  auto api_handler = std::make_unique<API_Handler>(type, library_path, logger);

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
