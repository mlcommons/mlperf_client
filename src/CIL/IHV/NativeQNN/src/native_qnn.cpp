/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "native_qnn.h"

#include <functional>
#include <nlohmann/json.hpp>

#include "Genie/GenieCommon.h"
#include "llm/llm_inference.h"

#define API_IHV_NATIVE_QNN_NAME "NativeQNN"
#define API_IHV_NATIVE_QNN_VERSION "0.0.1"

namespace cil {
namespace IHV {

cil::IHV::NativeQNN::NativeQNN(const API_IHV_Setup_t& api) {
  std::string model_name = api.model_name;
  std::string ep_name = api.ep_name;
  std::string model_path = api.model_path;
  std::string ep_settings_str = api.ep_settings;

  NativeQnnExecutionProviderSettings ep_settings(
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

  if (model_name == "llama2" || model_name == "llama3" || model_name == "phi3.5") {
    inference_ = std::make_shared<infer::LlmInference>(model_path, model_name,
                                                          ep_settings, logger);

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

DEFINE_IHV_CLASS_ENUMERATE_DEVICES_IMPL(cil::IHV::NativeQNN)

bool cil::IHV::NativeQNN::Init(const API_IHV_Init_t& api) {
  if (inference_ == nullptr) {
    return false;
  }

  if (auto llm_inference =
          std::dynamic_pointer_cast<infer::LlmInference>(inference_);
      llm_inference != nullptr) {
    // Load model configs
    std::string model_config = api.model_config;
    llm_inference->Init(nlohmann::json::parse(api.model_config));
  } else {
    inference_->Init();
  }

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::NativeQNN::Prepare(const API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "NativeQNN->Prepare called before it has been created!");
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

bool cil::IHV::NativeQNN::Infer(API_IHV_Infer_t& api) {
  if (inference_ == nullptr) {
    return false;
  }

  std::string error_message;

  if (auto llm_inference =
          std::dynamic_pointer_cast<infer::LlmInference>(inference_);
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

    llm_inference->Run(api, token_callback_wrapper);
  }

  if (auto error_message = inference_->GetErrorMessage();
      !error_message.empty()) {
    api.logger(api.context, API_IHV_LogLevel::API_IHV_ERROR,
               error_message.c_str());
    return false;
  }

  return true;
}

bool cil::IHV::NativeQNN::Reset(const API_IHV_Simple_t& api) {
  if (inference_ == nullptr) {
    api.logger(api.context, API_IHV_ERROR,
               "NativeQNN->Reset called before it has been created!");
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

bool cil::IHV::NativeQNN::Deinit(const API_IHV_Deinit_t& api) {
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

DEFINE_API_IHV_BASIC_IMPL(cil::IHV::NativeQNN, API_IHV_NATIVE_QNN_NAME,
                          API_IHV_NATIVE_QNN_VERSION)
