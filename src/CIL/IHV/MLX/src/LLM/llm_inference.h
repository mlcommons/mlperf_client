#pragma once

#include <vector>

#include "../api.h"
#include "llm/llama_config.h"
#include "base_inference_common.h"
#include "../mlx_config.h"


namespace cil {
namespace IHV {
namespace infer {

class LLMInference : public cil::infer::BaseInferenceCommon {
public:
    LLMInference(
        const std::string& model_path,
        const std::string& model_name,
        const MLXExecutionProviderSettings& ep_settings,
        Logger logger
    );
    ~LLMInference() = default;

    void Init(const nlohmann::json& model_config);
    void Prepare() override;
    std::span<uint32_t const> Run(const std::span<uint32_t const> input_data,
                                  std::function<void(uint32_t)> token_callback);

    void Reset() override;
    void Deinit() override;

private:
    std::filesystem::path FindSafeTensorsDir(const std::filesystem::path& dir);

    const MLXExecutionProviderSettings ep_settings_;
    cil::infer::LlamaConfig config_;
    void* inference_ = nullptr;
    void* kv_cache_ = nullptr;
    std::vector<uint32_t> output_data_;
    std::filesystem::path model_directory_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
