#include "mlx.h"

#include "llm/llm_inference.h"

#define API_IHV_MLX_NAME "MLX"
#define API_IHV_MLX_VERSION "0.0.1"

namespace cil {
namespace IHV {

cil::IHV::MLX::MLX(const API_IHV_Setup_t& api) {
  std::string model_name = api.model_name;
  std::string model_path = api.model_path;
  std::string ep_settings_str = api.ep_settings;
  nlohmann::json ep_settings = nlohmann::json::parse(ep_settings_str);

  auto logger = [=](cil::LogLevel level, std::string message) {
    api.logger(api.context, static_cast<API_IHV_LogLevel>(level),
               message.c_str());
  };

  if (!model_name.compare("llama2") || !model_name.compare("llama3") ||
      !model_name.compare("phi3.5") || !model_name.compare("phi4")) {
    inference_ = std::make_shared<infer::LLMInference>(model_path, model_name,
                                                       ep_settings, logger);

    auto error_message = inference_->GetErrorMessage();
    if (!error_message.empty()) {
      api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
                 error_message.c_str());
      inference_.reset();
      return;
    }
  } else {
    auto error = "Model " + model_name + " is not supported";
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL, error.c_str());
    return;
  }

  device_type_ = inference_->GetDeviceType();
  if (device_type_.empty()) {
    std::string error = "Device type was not set";
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL, error.c_str());
  }
}

DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(cil::IHV::MLX);

bool cil::IHV::MLX::Init(const API_IHV_Init_t& api) {
  if (inference_ == nullptr) {
    return false;
  }

  if (auto llm_inference =
          std::dynamic_pointer_cast<infer::LLMInference>(inference_);
      llm_inference != nullptr) {
    // Load model configs
    std::string model_config = api.model_config;
    llm_inference->Init(nlohmann::json::parse(api.model_config));
  } else
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
               "Inference engine is not instantiated correctly");

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::MLX::Prepare(const struct API_IHV_Simple_t& api) {
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

bool cil::IHV::MLX::Infer(API_IHV_Infer_t& api) {
  if (inference_ == nullptr) {
    return false;
  }
  if (auto llm_inference =
          std::dynamic_pointer_cast<infer::LLMInference>(inference_);
      llm_inference != nullptr) {
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
    std::vector<uint32_t> input(input_data,
                                input_data + api.io_data->input_size);
    const auto& output = llm_inference->Run(input, token_callback_wrapper);
    api.io_data->output = output.data();
    api.io_data->output_size = output.size();
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

bool cil::IHV::MLX::Reset(const struct API_IHV_Simple_t& api) {
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

bool cil::IHV::MLX::Deinit(const API_IHV_Deinit_t& api) {
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

DEFINE_API_IHV_BASIC_IMPL(cil::IHV::MLX, API_IHV_MLX_NAME, API_IHV_MLX_VERSION)