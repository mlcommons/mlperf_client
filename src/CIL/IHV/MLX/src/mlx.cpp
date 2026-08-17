#include "mlx.h"


#include "Image/image_inference.h"
#include "llm/llm_inference.h"

#define API_IHV_MLX_NAME "MLX"
#define API_IHV_MLX_VERSION "0.0.1"

namespace cil {
namespace IHV {

cil::IHV::MLX::MLX(const API_IHV_Setup_t& api) {
  const std::string scenario_name = api.scenario_name;
  std::string model_path = api.model_path;
  std::string ep_settings_str = api.ep_settings;
  const std::string model_name = api.model_base_name;
  const std::string model_family =
      [](const std::string& base_name) -> std::string {
    if (base_name.starts_with("llama_2")) return "llama2";
    if (base_name.starts_with("llama_3")) return "llama3";
    if (base_name.starts_with("phi_3_5")) return "phi3.5";
    if (base_name.starts_with("phi_4")) return "phi4";
    if (base_name.starts_with("qwen_3")) return "qwen3";
    return base_name;
  }(model_name);

  nlohmann::json ep_settings = nlohmann::json::parse(ep_settings_str);

  auto logger = [=](cil::LogLevel level, std::string message) {
    api.logger(api.context, static_cast<API_IHV_LogLevel>(level),
               message.c_str());
  };

  bool is_canonical_scenario = false;
  IHV_VALIDATE_SCENARIO(api, scenario_name, is_canonical_scenario);
  IHV_VALIDATE_MODEL_BASE_NAME(
      api, model_name, scenario_name, is_canonical_scenario, "llama_2_7b_chat",
      "llama_3_1_8b_instruct", "phi_3_5_mini_instruct", "phi_4_reasoning_14b",
      "phi_4_mini_instruct", "qwen_3_8b", "flux_2_klein_4b");

  // Image (txt2img) models run through the diffusion path; everything else is
  // an LLM. The model base name drives the split (e.g. "flux_2_klein_4b").
  const bool is_image_model = model_name.starts_with("flux");
  if (is_image_model) {
    inference_ = std::make_shared<infer::ImageInference>(
        model_path, model_name, ep_settings, logger);
  } else {
    inference_ = std::make_shared<infer::LLMInference>(model_path, model_family,
                                                       ep_settings, logger);
  }

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    inference_.reset();
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
  } else if (auto image_inference =
                 std::dynamic_pointer_cast<infer::ImageInference>(inference_);
             image_inference != nullptr) {
    image_inference->Init(nlohmann::json::parse(api.model_config));
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
  } else if (auto image_inference =
                 std::dynamic_pointer_cast<infer::ImageInference>(inference_);
             image_inference != nullptr) {
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
    auto submodule_wrapper = [&](const std::string& name, double ms) {
      API_IHV_ImageStep_Event_t event{API_IHV_ImageStep_Submodule, 0, 0,
                                      name.c_str(), ms};
      step_cb(cb_obj, &event);
    };

    std::string raw_input(static_cast<const char*>(api.io_data->input),
                          api.io_data->input_size);
    std::string prompt;
    std::string negative_prompt;
    try {
      auto input_json = nlohmann::json::parse(raw_input);
      prompt = input_json.value("prompt", raw_input);
      negative_prompt = input_json.value("negative_prompt", "");
    } catch (...) {
      prompt = raw_input;
    }

    emit(API_IHV_ImageStep_Start, 0, image_inference->GetSteps());
    image_inference->Run(prompt, negative_prompt, step_wrapper,
                         submodule_wrapper);
    emit(API_IHV_ImageStep_End, total_steps_captured, total_steps_captured);

    api.io_data->output = image_inference->GetOutputData();
    api.io_data->output_size =
        static_cast<unsigned>(image_inference->GetOutputSize());
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