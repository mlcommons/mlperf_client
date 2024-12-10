#pragma once

#include <openvino/genai/llm_pipeline.hpp>
#include <openvino/openvino.hpp>

#include "base_inference_common.h"
#include "native_openvino_config.h"

namespace cil {
namespace IHV {
namespace infer {

class BaseInference : public cil::infer::BaseInferenceCommon {
 public:
  BaseInference(const std::string& model_path,
                const NativeOpenVINOExecutionProviderSettings& ep_settings,
                cil::Logger logger, const std::string& deps_dir);

 protected:
  const NativeOpenVINOExecutionProviderSettings ep_settings_;

  std::unique_ptr<ov::Core> core_;

  std::string plugins_location_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
