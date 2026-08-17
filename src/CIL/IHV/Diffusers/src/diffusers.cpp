#include "diffusers.h"

#include "Image/image_inference.h"
#include "diffusers_config.h"

namespace cil {
namespace IHV {

cil::IHV::Diffusers::Diffusers(const API_IHV_Setup_t& api) {
  const std::string ep_name = api.ep_name;
  const std::string scenario_name = api.scenario_name;
  const std::string model_path = api.model_path;
  const std::string deps_dir = api.deps_dir;
  std::string ep_settings_str = api.ep_settings;
  const std::string model_name = api.model_base_name;

  DiffusersExecutionProviderSettings ep_settings(
      nlohmann::json::parse(ep_settings_str));

  device_type_ = ep_settings.GetDeviceType();

  auto logger = [=](cil::LogLevel level, std::string message) {
    api.logger(api.context, static_cast<API_IHV_LogLevel>(level),
               message.c_str());
  };

  bool is_canonical_scenario = false;
  IHV_VALIDATE_SCENARIO(api, scenario_name, is_canonical_scenario);

  inference_ = std::make_shared<infer::ImageInference>(
      model_path, model_name, ep_name, deps_dir, ep_settings, logger);

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_ERROR, error_message.c_str());
    inference_.reset();
  }
}

DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(cil::IHV::Diffusers)

bool cil::IHV::Diffusers::Init(const API_IHV_Init_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "Diffusers->Init called before it has been created!");
    return false;
  }

  const std::optional<API_IHV_DeviceID_t> device_id =
      api.device_id != nullptr
          ? std::optional<API_IHV_DeviceID_t>{*api.device_id}
          : std::nullopt;

  auto img_inference =
      std::dynamic_pointer_cast<infer::ImageInference>(inference_);
  if (img_inference) {
    img_inference->Init(nlohmann::json::parse(api.model_config), device_id);
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

bool cil::IHV::Diffusers::Prepare(const API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "Diffusers->Prepare called before it has been created!");
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

bool cil::IHV::Diffusers::Infer(API_IHV_Infer_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "Diffusers->Infer called before it has been created!");
    return false;
  }

  auto img_inference =
      std::dynamic_pointer_cast<infer::ImageInference>(inference_);
  if (!img_inference) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_FATAL,
               "Diffusers EP only supports image inference");
    return false;
  }

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

  emit(API_IHV_ImageStep_Start, 0, img_inference->GetSteps());
  img_inference->Run(prompt, negative_prompt, step_wrapper, submodule_wrapper);
  emit(API_IHV_ImageStep_End, total_steps_captured, total_steps_captured);

  if (auto err = img_inference->GetErrorMessage(); !err.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR, err.c_str());
    return false;
  }

  api.io_data->output = img_inference->GetOutputData();
  api.io_data->output_size =
      static_cast<unsigned>(img_inference->GetOutputSize());

  return true;
}

bool cil::IHV::Diffusers::Reset(const API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "Diffusers->Reset called before it has been created!");
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

bool cil::IHV::Diffusers::Deinit(const API_IHV_Deinit_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "Diffusers->Deinit called before it has been created!");
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

DEFINE_API_IHV_BASIC_IMPL(cil::IHV::Diffusers, API_IHV_DIFFUSERS,
                          API_IHV_DIFFUSERS_VERSION)
