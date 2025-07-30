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


#include "base_inference_common.h"
#include "native_qnn_config.h"

namespace cil {
namespace IHV {
namespace infer {

class BaseInference : public cil::infer::BaseInferenceCommon {
 public:
  BaseInference(const std::string& model_path,
                const std::string& model_name,
                const NativeQnnExecutionProviderSettings& ep_settings,
                cil::Logger logger);

  virtual ~BaseInference() = default;

 protected:
  const NativeQnnExecutionProviderSettings ep_settings_;

};
}  // namespace infer
}  // namespace IHV
}  // namespace cil
