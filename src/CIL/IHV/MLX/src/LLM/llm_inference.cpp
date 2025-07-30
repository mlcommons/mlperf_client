#include "llm_inference.h"
#include <algorithm>

namespace cil {
namespace IHV {
namespace infer {

LLMInference::LLMInference(
    const std::string& model_path,
    const std::string& model_name,
    const MLXExecutionProviderSettings& ep_settings,
    Logger logger
) : BaseInferenceCommon(model_path, model_name, logger),
    ep_settings_(ep_settings),
    config_(logger) {
    
    device_type_ = ep_settings.GetDeviceType();
    if (device_type_.empty()) {
        device_type_ = "GPU";
    }

    if (!model_path.empty())
      model_directory_ =
          FindSafeTensorsDir(std::filesystem::path(model_path_).parent_path());
}

std::filesystem::path LLMInference::FindSafeTensorsDir(const std::filesystem::path& dir) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (std::filesystem::is_regular_file(entry) && entry.path().extension() == ".safetensors") {
            return entry.path().parent_path();
        }
    }

    return std::filesystem::path();
}

void LLMInference::Init(const nlohmann::json &model_config) {
    bool loaded = config_.LoadFromJson(model_config);
    if (!loaded) {
        error_message_ = "Failed to load model configuration";
        logger_(LogLevel::kError, error_message_);
        return;
    }

    inference_ = llm_create(
        model_path_.c_str(),
        model_directory_.c_str(),
        model_name_.c_str(),
        device_type_.c_str()
    );
}

void LLMInference::Prepare() {
    output_data_.clear();
    kv_cache_ = llm_create_kvcache(inference_, model_name_.c_str());
}

std::span<uint32_t const> LLMInference::Run(const std::span<uint32_t const> input_data,
                                               std::function<void(uint32_t)> token_callback) {
    
    auto start_time = std::chrono::high_resolution_clock::now();

    size_t n_all_tokens = input_data.size() + config_.search.max_length;
    if (n_all_tokens > config_.search.max_total_length) {
        error_message_ = "Input tokens + max_length exceeds max_total_length";
        logger_(LogLevel::kError, error_message_);
        return output_data_;
    }

    std::string search_method;
    if (config_.search.method == cil::infer::SearchMethod::kGreedy) {
        search_method = "greedy";
    }
    else {
        error_message_ = "Search method is not implemented";
        logger_(LogLevel::kError, error_message_);
        return output_data_;
    }

    std::vector<uint32_t> model_input(input_data.begin(), input_data.end());

    output_data_.reserve(config_.search.max_length);
    bool first_iteration = true;
    for (size_t n_cur = input_data.size(); n_cur < n_all_tokens; n_cur++) {
        auto next_token = llm_predict(
            inference_,
            model_input.data(),
            static_cast<int64_t>(model_input.size()),
            kv_cache_,
            model_name_.c_str(),
            search_method.c_str(),
            first_iteration
        );
        first_iteration = false;
        if (next_token == -1) {
            error_message_ = "Failed to retrieve next token";
            logger_(LogLevel::kError, error_message_);
            return output_data_;
        }
        token_callback(next_token);
        output_data_.push_back(next_token);
        model_input = {static_cast<uint32_t>(next_token)};
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    logger_(LogLevel::kInfo,
            "Execution finished, EP: MLX, model: " + model_path_);
    LogTime("Inference time: ", start_time, end_time);
    error_message_.clear();

    return output_data_;
}

void LLMInference::Reset() {
    if (kv_cache_ != nullptr) {
        llm_free_kvcache(kv_cache_);
        kv_cache_ = nullptr;
    }
}

void LLMInference::Deinit() {
    if (inference_ != nullptr) {
        llm_free_model(inference_, model_name_.c_str());
    }
    if (kv_cache_ != nullptr) {
        llm_free_kvcache(kv_cache_);
        kv_cache_ = nullptr;
    }

    llm_clear_cache();
    error_message_.clear();
}

}  // infer
}  // IHV
}  // cil
