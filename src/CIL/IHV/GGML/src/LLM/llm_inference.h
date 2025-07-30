#ifndef LLAMA2_INFERENCE_H_
#define LLAMA2_INFERENCE_H_

#include "../ggml_eps_config.h"
#include "base_inference_common.h"
#include "llm/llama_config.h"

class llama_model;
class llama_context;

namespace cil {
namespace IHV {

namespace infer {

class LLMInference : public cil::infer::BaseInferenceCommon {
 public:
  LLMInference(const std::string& model_path,
                  const std::string& model_name,
                  const GGMLBasedExecutionProviderSettings& ep_settings,
                  Logger logger);
  ~LLMInference() = default;

  bool GPULayersDeductionIsInProgress() const;

  void Init(const nlohmann::json& model_config);
  void Run(const std::span<uint32_t const> input_data,
           std::function<void(uint32_t)> token_callback);
  void Reset() override;
  void Deinit() override;

 private:
  bool LoadModelAndContext(int gpu_layers);

  const GGMLBasedExecutionProviderSettings ep_settings_;
  cil::infer::LlamaConfig config_;

  llama_model* model_;
  llama_context* context_;

  std::chrono::high_resolution_clock::time_point start_time_;
  std::chrono::high_resolution_clock::time_point end_time_;

  bool gpu_layers_deduction_in_progress_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil

#endif  // !LLAMA2_INFERENCE_H_
