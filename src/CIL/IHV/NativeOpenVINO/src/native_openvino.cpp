#include "native_openvino.h"

#include "llama2/llama2_inference.h"
#include "native_openvino_config.h"

#define API_IHV_NATIVE_OPENVINO_NAME "NativeOpenVINO"
#define API_IHV_NATIVE_OPENVINO_VERSION "0.0.1"

namespace cil {
namespace IHV {

cil::IHV::NativeOpenVINO::NativeOpenVINO(const API_IHV_Setup_t& api) {
  std::string model_name = api.model_name;
  std::string ep_name = api.ep_name;
  std::string model_path = api.model_path;
  std::string ep_settings_str = api.ep_settings;
  NativeOpenVINOExecutionProviderSettings ep_settings(
      nlohmann::json::parse(ep_settings_str));

  device_type_ = ep_settings.GetDeviceType();

  auto logger = [=](cil::LogLevel level, std::string message) {
    api.logger(api.context, static_cast<API_IHV_LogLevel>(level),
               message.c_str());
  };

  if (device_type_.empty()) {
    std::string error = "Device type was not set, aborting";
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL, error.c_str());
    return;
  }

  if (model_name == "llama2") {
    inference_ = std::make_shared<infer::Llama2Inference>(
        model_path, ep_settings, logger, api.deps_dir);

    auto error_message = inference_->GetErrorMessage();
    if (!error_message.empty()) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 error_message.c_str());
      inference_.reset();
    }

    return;
  }

  auto error = "Model " + model_name + " is not supported";
  api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL, error.c_str());
}

DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(cil::IHV::NativeOpenVINO)

bool cil::IHV::NativeOpenVINO::Init(const API_IHV_Init_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
               "IHV NativeOpenVINO Init: Inference object is nullptr!");
    return false;
  }

  if (auto llama2_inference =
          std::dynamic_pointer_cast<infer::Llama2Inference>(inference_);
      llama2_inference != nullptr) {
    // Load model configs
    const std::string model_config = api.model_config;
    const std::optional<API_IHV_DeviceID_t> device_id =
      api.device_id != nullptr ?
      std::optional<API_IHV_DeviceID_t>{*api.device_id} : std::nullopt;
    llama2_inference->Init(nlohmann::json::parse(api.model_config),
                           device_id);
  } else {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
               "IHV NativeOpenVINO Init: The inference model is unknown!");
    return false;
  }

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::NativeOpenVINO::Prepare(const struct API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    return false;
  }

  inference_->Prepare();

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::NativeOpenVINO::Infer(API_IHV_Infer_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_INFO,
               "NativeOpenVINO::Infer null pointer detected");
    return false;
  }

  std::string error_message;

  if (auto llama2_inference =
          std::dynamic_pointer_cast<infer::Llama2Inference>(inference_)) {
    // Create token callback function wrapper
    if (api.io_data->callback.type != API_IHV_Callback_Type::API_IHV_CB_Token ||
        api.io_data->callback.function == nullptr ||
        api.io_data->callback.object == nullptr) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 "Callback function info invalid");
      return false;
    }
    API_IHV_Token_Callback_func callback =
        reinterpret_cast<API_IHV_Token_Callback_func>(
            api.io_data->callback.function);
    void* callback_obj = api.io_data->callback.object;
    auto token_callback_wrapper = [&](uint32_t token) {
      callback(callback_obj, token);
    };

    auto input_data = static_cast<const uint32_t* const>(api.io_data->input);
    std::span<const uint32_t> input(input_data,
                                    input_data + api.io_data->input_size);

    llama2_inference->Run(input, token_callback_wrapper);

    if (auto error_message = llama2_inference->GetErrorMessage();
        !error_message.empty()) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 error_message.c_str());
      return false;
    }

    error_message = llama2_inference->GetErrorMessage();

  } else {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
               "The inference model is unknown!");
    return false;
  }

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::NativeOpenVINO::Reset(const struct API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    return false;
  }

  inference_->Reset();

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::NativeOpenVINO::Deinit(const API_IHV_Deinit_t& api) {
  if (inference_ == nullptr) {
    return false;
  }

  inference_->Deinit();

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

}  // namespace IHV
}  // namespace cil

DEFINE_API_IHV_BASIC_IMPL(cil::IHV::NativeOpenVINO,
                          API_IHV_NATIVE_OPENVINO_NAME,
                          API_IHV_NATIVE_OPENVINO_VERSION)

#include <Windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL,
                    DWORD fdwReason,
                    LPVOID lpvReserved ) {
  if (fdwReason == DLL_PROCESS_ATTACH) {
    // FIXME Workaround to be removed in the next version
    // Increase library counter to avoid static objects
    // destructors ordering issue.
    LoadLibraryExA("IHV_NativeOpenVINO.dll",
                   0,
                   LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  }
  return TRUE;
}
