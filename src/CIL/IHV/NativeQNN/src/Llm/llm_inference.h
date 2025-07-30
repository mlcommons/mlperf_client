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

#pragma once

#include <nlohmann/json.hpp>

#include "../../../IHV.h"
#include "../base_inference.h"
#include "Genie/GenieCommon.h"
#include "Genie/GenieDialog.h"
#include "Llm/llama_config.h"
#include "QNN/GenAiTransformer/QnnGenAiTransformerCommon.h"
#include "QNN/QnnInterface.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cil {
namespace IHV {

namespace infer {

class LlmInference : public BaseInference {
 public:
  LlmInference(const std::string& model_path,
                  const std::string& model_name,
                  const NativeQnnExecutionProviderSettings& ep_settings,
                  cil::Logger logger);

  void FillConfigString(const nlohmann::json& model_config);

  void Init(const nlohmann::json& model_config);

  void Run(API_IHV_Infer_t& api, std::function<void(uint32_t)> token_callback);

  void Reset() override;

  void Deinit() override;

  static void tokenToTokenCallback(const uint32_t* token, 
    const uint32_t tokensLength,
    const GenieDialog_SentenceCode_t sentenceCode, 
    const void*);

  static inline std::function<void(uint32_t)> token_callback_global;

 private:
  std::string model_path_;
  std::string model_folder_;
  std::string model_name_;
  std::string device_type_;
  std::string configString_;

  std::map<std::string, HMODULE> genie_extra_modules_;

  GenieDialog_Handle_t dialogHandle{nullptr};

  std::string formConfigStringLlama2(const nlohmann::json& model_config,
                                     const std::string& device_type_,
                                     const std::string& model_folder_,
                                     const std::string& model_path_);
  std::string formConfigStringLlama3(const nlohmann::json& model_config,
                                     const std::string& device_type_,
                                     const std::string& model_folder_,
                                     const std::string& model_path_);
  std::string formConfigStringPhi3_5(const nlohmann::json& model_config,
                                     const std::string& device_type_,
                                     const std::string& model_folder_,
                                     const std::string& model_path_);
};
}  // namespace infer
}  // namespace IHV
}  // namespace cil

