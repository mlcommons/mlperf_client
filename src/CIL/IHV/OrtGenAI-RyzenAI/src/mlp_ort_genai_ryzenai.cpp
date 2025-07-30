#pragma once
#include "mlp_ort_genai_ryzenai.h"

#include "LLM/llm_inference.h"
#include "mlp_ort_genai_ryzenai_config.h"

namespace cil {
namespace IHV {

cil::IHV::OrtGenAIRyzenAI::OrtGenAIRyzenAI(const API_IHV_Setup_t& api) {
  const std::string model_name = api.model_name;
  const std::string ep_name = api.ep_name;
  const std::string deps_dir = api.deps_dir;
  const std::string model_path = api.model_path;

  std::string ep_settings_str = api.ep_settings;
  OrtGenAIExecutionProviderSettings ep_settings(
      nlohmann::json::parse(ep_settings_str));

  device_type_ = ep_settings.GetDeviceType();

  auto logger = [=](cil::LogLevel level, std::string message) {
    api.logger(api.context, static_cast<API_IHV_LogLevel>(level),
               message.c_str());
  };

  if (!model_name.compare("llama2") || !model_name.compare("llama3") || !model_name.compare("phi3.5") ||
      !model_name.compare("phi4")) {
    inference_ = std::make_shared<infer::LLMInference>(
        model_path, model_name, ep_name, deps_dir, ep_settings, logger);

    auto error_message = inference_->GetErrorMessage();
    if (!error_message.empty()) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 error_message.c_str());
      inference_.reset();
    }
    
    return;
  }

  api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
             std::format("Model {} is not supported", model_name).c_str());
}

DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(cil::IHV::OrtGenAIRyzenAI)

bool cil::IHV::OrtGenAIRyzenAI::Init(const API_IHV_Init_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "OrtGenAI->Init called before it has been created!");
    return false;
  }

  if (auto llama2_inference =
          std::dynamic_pointer_cast<infer::LLMInference>(inference_)) {
    const std::optional<API_IHV_DeviceID_t> device_id =
      api.device_id != nullptr ?
      std::optional<API_IHV_DeviceID_t>{*api.device_id} : std::nullopt;
    llama2_inference->Init(nlohmann::json::parse(api.model_config),
                           device_id);

  } else {
    inference_->Init();
  }
  auto error_message = inference_->GetErrorMessage();
  if (!error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::OrtGenAIRyzenAI::Prepare(const API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "OrtGenAI->Prepare called before it has been created!");
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

bool cil::IHV::OrtGenAIRyzenAI::Infer(API_IHV_Infer_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "OrtGenAI->Infer called before it has been created!");
    return false;
  }

  if (auto llama2_inference =
          std::dynamic_pointer_cast<infer::LLMInference>(inference_)) {
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

    const auto& output = llama2_inference->Run(
        static_cast<const int32_t* const>(api.io_data->input),
        api.io_data->input_size, token_callback_wrapper);

    if (output.size() == 0) {
      api.logger(
          api.context, API_IHV_ERROR,
          (std::string("OrtGenAI Run returned zero tokens! Error message: ") +
           llama2_inference->GetErrorMessage())
              .c_str());
      return false;
    }

  } else if (auto error_message = inference_->GetErrorMessage();
             !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::OrtGenAIRyzenAI::Reset(const API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "OrtGenAI->Reset called before it has been created!");
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

bool cil::IHV::OrtGenAIRyzenAI::Deinit(const API_IHV_Deinit_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "OrtGenAI->Deinit called before it has been created!");
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

DEFINE_API_IHV_BASIC_IMPL(cil::IHV::OrtGenAIRyzenAI, API_IHV_ORT_GENAI,
                          API_IHV_ORT_GENAI_VERSION)
