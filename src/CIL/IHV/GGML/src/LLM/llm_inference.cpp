#include "llm_inference.h"

#include "../../common/common.h"
#include "llama.h"

namespace cil {
namespace IHV {
namespace infer {

LLMInference::LLMInference(
    const std::string& model_path, const std::string& model_name,
    const GGMLBasedExecutionProviderSettings& ep_settings, Logger logger)
    : BaseInferenceCommon(model_path, model_name, logger),
      ep_settings_(ep_settings),
      config_(logger),
      model_(nullptr),
      context_(nullptr),
      gpu_layers_deduction_in_progress_(false) {
  device_type_ = "CPU";
  if (ep_settings.GetDeviceType() != "CPU" && llama_supports_gpu_offload())
    device_type_ = "GPU";

  static auto logging_function = [](ggml_log_level level, const char* text,
                                    void* user_data) {
    auto llm_inference = static_cast<LLMInference*>(user_data);
    Logger logger = llm_inference->GetLogger();
    if (level == ggml_log_level::GGML_LOG_LEVEL_ERROR)
      logger(llm_inference->GPULayersDeductionIsInProgress()
                 ? LogLevel::kWarning
                 : LogLevel::kError,
             text);
  };
  llama_log_set(logging_function, this);

  llama_backend_init();

  llama_numa_init(GGML_NUMA_STRATEGY_DISABLED);
}

void LLMInference::Init(const nlohmann::json& model_config) {
  bool loaded = config_.LoadFromJson(model_config);
  if (!loaded) {
    logger_(LogLevel::kError, "Failed to load model configuration");
    error_message_ = "Failed to load model configuration";
    return;
  }

  int initial_gpu_layers =
      device_type_ == "GPU" ? ep_settings_.GetGPULayers().value_or(999) : 0;

  gpu_layers_deduction_in_progress_ = true;

  // Try to load the model and context with the specified gpu_layers
  if (LoadModelAndContext(initial_gpu_layers)) {
    gpu_layers_deduction_in_progress_ = false;
    return;
  }

  // If initial load failed and gpu_layers > 0, attempt fallback with fewer
  // layers
  logger_(LogLevel::kWarning,
          "Initial GPU model or context load failed. Trying fallback...");

  // Load the model with 0 GPU layers to retrieve the total number of layers
  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = 0;
  model_params.main_gpu = 0;
  model_params.use_mmap = ep_settings_.GetNoMmap().value_or(false);
  if (ep_settings_.GetNoMmap().value_or(false))
    logger_(LogLevel::kInfo, "Not using mmap!");
  model_ = llama_load_model_from_file(model_path_.c_str(), model_params);
  if (!model_) {
    logger_(LogLevel::kError, "Failed to load the model");
    error_message_ = "Failed to load the model";
    return;
  }

  int total_layers = llama_model_n_layer(model_);
  llama_free_model(model_);
  model_ = nullptr;

  // Perform binary search to find the maximum number of GPU layers that fit in
  // memory
  int low = 1, high = total_layers, best = 0;
  while (low <= high) {
    int mid = (low + high) / 2;
    if (LoadModelAndContext(mid)) {
      llama_free(context_);
      llama_free_model(model_);
      context_ = nullptr;
      model_ = nullptr;
      best = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  gpu_layers_deduction_in_progress_ = false;

  // Apply a safety margin to avoid crashes during inference
  best = std::max(best - 2, 0);

  if (!LoadModelAndContext(best)) {
    logger_(LogLevel::kError,
            "Failed to load the model after multiple attempts");
    error_message_ = "Failed to load the model";
    return;
  }

  logger_(LogLevel::kInfo, "Successfully loaded model with " +
                               std::to_string(best) + " GPU layers");
}

bool LLMInference::GPULayersDeductionIsInProgress() const {
  return gpu_layers_deduction_in_progress_;
}

bool LLMInference::LoadModelAndContext(int gpu_layers) {
  llama_model_params model_params = llama_model_default_params();
  model_params.main_gpu = 0;
  model_params.n_gpu_layers = gpu_layers;
  model_params.split_mode = LLAMA_SPLIT_MODE_NONE;
  model_ = llama_load_model_from_file(model_path_.c_str(), model_params);
  if (!model_) return false;

  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = config_.model.context_length;
  ctx_params.n_batch =
      config_.search.max_total_length - config_.search.max_length;

  ctx_params.flash_attn = ep_settings_.GetFa().value_or(false);
  if (ep_settings_.GetFa().value_or(false))
    logger_(LogLevel::kInfo, "Using Flash attention!");
  context_ = llama_new_context_with_model(model_, ctx_params);
  if (!context_) {
    llama_free_model(model_);
    model_ = nullptr;
    return false;
  }

  return true;
}

void LLMInference::Run(const std::span<uint32_t const> input_data,
                          std::function<void(uint32_t)> token_callback) {
  start_time_ = std::chrono::high_resolution_clock::now();

  if (input_data.size() == 0) {
    error_message_ = "Input is empty, no tokens to decode";
    logger_(LogLevel::kError, error_message_);
    return;
  }

  int n_all_tokens = input_data.size() + config_.search.max_length;

  if (n_all_tokens > config_.search.max_total_length) {
    error_message_ = "Input tokens + max_length exceeds max_total_length";
    logger_(LogLevel::kError, error_message_);
    return;
  }

  // Make sure the KV cache is big enough to hold all the prompt and generated
  // tokens
  if (n_all_tokens > llama_n_ctx(context_)) {
    error_message_ = "KV cache size is too small";
    logger_(LogLevel::kError, error_message_);

    return;
  }

  auto chain_params = llama_sampler_chain_default_params();
  auto* sampler_chain = llama_sampler_chain_init(chain_params);

  switch (config_.search.method) {
    case cil::infer::SearchMethod::kGreedy: {
      auto* greedy_sampler = llama_sampler_init_greedy();
      llama_sampler_chain_add(sampler_chain, greedy_sampler);
      break;
    }
    case cil::infer::SearchMethod::kTopK: {
      auto* top_k_sampler = llama_sampler_init_top_k(config_.search.top_k);
      llama_sampler_chain_add(sampler_chain, top_k_sampler);
      break;
    }
    case cil::infer::SearchMethod::kTopP: {
      auto* top_p_sampler = llama_sampler_init_top_p(config_.search.top_p, 1);
      llama_sampler_chain_add(sampler_chain, top_p_sampler);
      break;
    }
    default: {
      error_message_ = "Unsupported sampling method";
      logger_(LogLevel::kError, error_message_);

      return;
    }
  }

  error_message_.clear();

  llama_batch batch = llama_batch_init(input_data.size(), 0, 1);

  for (int i = 0; i < input_data.size(); i++) {
    batch.token[i] = input_data[i];
    batch.pos[i] = i;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    batch.logits[i] = 0;
  }

  // Ensure n_tokens is set correctly
  batch.n_tokens = input_data.size();
  batch.logits[batch.n_tokens - 1] = 1;

  try {
    // Evaluate the prompt batch
    if (llama_decode(context_, batch) != 0) {
      logger_(LogLevel::kError, "Failed to evaluate the prompt");
      error_message_ = "Failed to evaluate the prompt";
      llama_batch_free(batch);
      llama_sampler_free(sampler_chain);
      return;
    }

    auto* vocab = llama_model_get_vocab(model_);

    int32_t idx = batch.n_tokens - 1;
    int n_cur = batch.n_tokens;

    while (n_cur < n_all_tokens) {
      batch.n_tokens = 0;

      if (idx < 0) break;

      const auto new_token_id =
          llama_sampler_sample(sampler_chain, context_, idx);

      token_callback(new_token_id);

      if (!llama_vocab_is_eog(vocab, new_token_id)) {
        idx = 0;

        batch.token[0] = new_token_id;
        batch.pos[0] = n_cur;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = true;

        batch.n_tokens = 1;
      } else
        idx = -1;

      n_cur += 1;

      if (idx >= 0 && llama_decode(context_, batch) != 0) {
        error_message_ = "Failed to evaluate the batch";
        logger_(LogLevel::kError, error_message_);

        break;
      }
    }

    llama_kv_self_clear(context_);

    end_time_ = std::chrono::high_resolution_clock::now();
  } catch (const std::exception& e) {
    error_message_ = "Exception during inference: " + std::string(e.what());
    logger_(LogLevel::kError, error_message_);

  } catch (...) {
    error_message_ = "Unknown exception during inference";
    logger_(LogLevel::kError, error_message_);
  }
  // No need to free batch here as we did it in the loop
  llama_batch_free(batch);
  llama_sampler_free(sampler_chain);
}

void LLMInference::Reset() {
  logger_(LogLevel::kInfo, "Execution finished, model: " + model_path_);
  LogTime("Inference time: ", start_time_, end_time_);
}

void LLMInference::Deinit() {
  if (context_) {
    llama_free(context_);
    context_ = nullptr;
  }
  if (model_) {
    llama_free_model(model_);
    model_ = nullptr;
  }
  llama_backend_free();
  error_message_.clear();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
