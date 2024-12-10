#pragma once

#include "base_inference_common.h"
#include "mlp_ort_genai_config.h"

namespace cil {
namespace IHV {
namespace infer {

class BaseInference : public cil::infer::BaseInferenceCommon {
 public:
  BaseInference(const std::string& model_path, const std::string& ep_name, 
                const std::string& deps_dir,
                const OrtGenAIExecutionProviderSettings& ep_settings,
                cil::Logger logger);

 protected:
  const OrtGenAIExecutionProviderSettings ep_settings_;
  const std::string ep_name_;
  const std::string deps_dir_;

  std::string plugins_location_;
};
}  // namespace infer
}  // namespace IHV
}  // namespace cil
