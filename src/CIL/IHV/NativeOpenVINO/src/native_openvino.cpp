#include "native_openvino.h"

#include "Image/image_inference.h"
#include "LLM/llm_inference.h"
#include "native_openvino_config.h"

#define API_IHV_NATIVE_OPENVINO_NAME "NativeOpenVINO"
#define API_IHV_NATIVE_OPENVINO_VERSION "0.0.1"

namespace cil {
namespace IHV {

cil::IHV::NativeOpenVINO::NativeOpenVINO(const API_IHV_Setup_t& api) {
  std::string ep_name = api.ep_name;
  const std::string scenario_name = api.scenario_name;
  std::string model_path = api.model_path;
  std::string ep_settings_str = api.ep_settings;
  const std::string model_name = api.model_base_name;

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

  bool is_canonical_scenario = false;
  IHV_VALIDATE_SCENARIO(api, scenario_name, is_canonical_scenario);

  const bool is_image_scenario =
      scenario_name == "txt2img" || scenario_name == "flux2klein";

  if (is_image_scenario) {
    inference_ = std::make_shared<infer::ImageInference>(
        model_path, model_name, ep_settings, logger, api.deps_dir);
  } else {
    inference_ = std::make_shared<infer::LLMInference>(
        model_path, model_name, ep_settings, logger, api.deps_dir);
  }

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    inference_.reset();
  }
}

DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(cil::IHV::NativeOpenVINO)

bool cil::IHV::NativeOpenVINO::Init(const API_IHV_Init_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
               "IHV NativeOpenVINO Init: Inference object is nullptr!");
    return false;
  }

  const std::optional<API_IHV_DeviceID_t> device_id =
      api.device_id != nullptr
          ? std::optional<API_IHV_DeviceID_t>{*api.device_id}
          : std::nullopt;

  if (auto img_inference =
          std::dynamic_pointer_cast<infer::ImageInference>(inference_);
      img_inference != nullptr) {
    img_inference->Init(nlohmann::json::parse(api.model_config), device_id);
  } else if (auto llm_inference =
                 std::dynamic_pointer_cast<infer::LLMInference>(inference_);
             llm_inference != nullptr) {
    llm_inference->Init(nlohmann::json::parse(api.model_config), device_id);
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

  if (auto img_inference =
          std::dynamic_pointer_cast<infer::ImageInference>(inference_)) {
    if (api.io_data->callback.type !=
            API_IHV_Callback_Type::API_IHV_CB_ImageStep ||
        api.io_data->callback.function == nullptr ||
        api.io_data->callback.object == nullptr) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 "ImageStep callback info invalid");
      return false;
    }

    API_IHV_ImageStep_Callback_func step_cb =
        reinterpret_cast<API_IHV_ImageStep_Callback_func>(
            api.io_data->callback.function);
    void* cb_obj = api.io_data->callback.object;

    auto emit = [&](API_IHV_ImageStep_Phase phase, unsigned step,
                    unsigned total_steps) {
      API_IHV_ImageStep_Event_t event{phase, step, total_steps, nullptr, 0.0};
      step_cb(cb_obj, &event);
    };

    unsigned total_steps_captured = 0;
    auto step_wrapper = [&](unsigned step, unsigned total_steps) {
      total_steps_captured = total_steps;
      emit(API_IHV_ImageStep_Step, step, total_steps);
    };

    std::string prompt(static_cast<const char*>(api.io_data->input),
                       api.io_data->input_size);

    emit(API_IHV_ImageStep_Start, 0, 0);
    img_inference->Run(prompt, step_wrapper);
    emit(API_IHV_ImageStep_End, total_steps_captured, total_steps_captured);

    if (auto err = img_inference->GetErrorMessage(); !err.empty()) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR, err.c_str());
      return false;
    }

    api.io_data->output = img_inference->GetOutputData();
    api.io_data->output_size =
        static_cast<unsigned>(img_inference->GetOutputSize());

  } else if (auto llm_inference =
                 std::dynamic_pointer_cast<infer::LLMInference>(inference_)) {
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

    llm_inference->Run(input, token_callback_wrapper);

    if (auto error_message = llm_inference->GetErrorMessage();
        !error_message.empty()) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 error_message.c_str());
      return false;
    }

    error_message = llm_inference->GetErrorMessage();

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

#ifdef _WIN32
#include <Windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  if (fdwReason == DLL_PROCESS_ATTACH) {
    // FIXME Workaround to be removed in the next version
    // Increase library counter to avoid static objects
    // destructors ordering issue.
    LoadLibraryExA("IHV_NativeOpenVINO.dll", 0,
                   LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  }
  return TRUE;
}
#endif