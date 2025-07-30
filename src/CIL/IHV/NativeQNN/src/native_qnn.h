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

#include "IHV_handler_class.h"

#include <memory>
#include <string>
#include <vector>

#include "native_qnn_config.h"
#include "base_inference.h"

namespace cil {
namespace IHV {
namespace infer {
class BaseInference;
}  // namespace infer
}  // namespace IHV
}  // namespace cil

namespace cil {
namespace IHV {

DECLARE_IHV_HANDLER_BASIC_CLASS(NativeQNN, infer::BaseInference)

}  // namespace IHV
}  // namespace cil

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API_IHV

#ifdef __cplusplus
}
#endif
