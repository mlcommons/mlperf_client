#include "base_inference.h"

namespace cil {
namespace IHV {
namespace infer {

BaseInference::BaseInference(
    const std::string& model_path, const std::string& ep_name,
    const std::string& deps_dir,
    const OrtGenAIExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInferenceCommon(model_path, logger),
      ep_settings_(ep_settings),
      ep_name_(ep_name),
      deps_dir_(deps_dir) {}

}  // namespace infer
}  // namespace IHV
}  // namespace cil