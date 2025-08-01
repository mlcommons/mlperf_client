#pragma once
#include <nlohmann/json.hpp>
#include <optional>

#include "../base_inference.h"
#include "llm/llama_config.h"
#include "ort_genai_c.h"

namespace cil {
namespace IHV {
namespace infer {

class LLMInference : public BaseInference {
 public:
  LLMInference(const std::string& model_path, 
               const std::string& model_name,
               const std::string& ep_name,
               const std::string& deps_dir,
               const OrtGenAIExecutionProviderSettings& ep_settings,
               cil::Logger logger);
  ~LLMInference() override;
  void Init(const nlohmann::json& model_config,
            std::optional<API_IHV_DeviceID_t> device_id);
  const std::vector<int32_t>& Run(const int32_t* const token_ptr,
                                  uint32_t token_count,
                                  std::function<void(uint32_t)> token_callback);
  void Deinit() override;

 private:
  void SetupGenaiConfigForDirectML();

  cil::infer::LlamaConfig config_;
  bool initialized_ = false;

  // Filled with tokens after Run call
  std::vector<int32_t> output_data_;

  // ORT Attributes
  OgaModel* oga_model_ = 0;
  OgaGeneratorParams* oga_gen_params_ = 0;
  OgaSequences* oga_benchmark_sequences_ = 0;
  OgaGenerator* oga_bench_generator_ = 0;

  bool device_set_ = false;
  int device_id_active_ = 0;  // default device is 0

  std::optional<std::pair<std::string, nlohmann::json>> backup_genai_config;
};
}  // namespace infer
}  // namespace IHV
}  // namespace cil
