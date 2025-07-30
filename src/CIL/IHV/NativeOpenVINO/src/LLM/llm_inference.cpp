#include "llm_inference.h"

#include <stdlib.h>

#include <filesystem>
#include <format>
#include <openvino/genai/llm_pipeline.hpp>

#include "../native_openvino_config.h"
#include "llm/llama_config.h"
#include "math_utils.h"

namespace {

constexpr size_t kMaxNpuAllocSize = 4032;

double CallbackStreamer::GetMsCI(
    size_t start_token, CallbackStreamer::time_point start_time) const {
  if (start_token >= token_time_.size()) {
    return 0;
  }

  auto cur_time = start_time;
  std::vector<double> diff_time_ms(token_time_.size());
  std::transform(token_time_.begin(), token_time_.end(), diff_time_ms.begin(),
                 [cur_time = start_time](auto t) mutable {
                   auto r = GetDurationInMs(cur_time, t);
                   cur_time = t;
                   return r;
                 });

  const auto mean = std::accumulate(diff_time_ms.begin() + start_token,
                                    diff_time_ms.end(), 0.0) /
                    static_cast<double>(diff_time_ms.size() - start_token);
  const auto sum =
      std::accumulate(diff_time_ms.begin() + start_token, diff_time_ms.end(),
                      0.0, [&mean](double acc, double val) -> double {
                        return acc + (val - mean) * (val - mean);
                      });

  return 2.0 * std::sqrt(sum / static_cast<double>(diff_time_ms.size() -
                                                   start_token));
}

}  // namespace

using namespace cil::infer;

namespace cil {
namespace IHV {
namespace infer {

LLMInference::LLMInference(
    const std::string& model_path, const std::string& model_name,
    const NativeOpenVINOExecutionProviderSettings& ep_settings,
    cil::Logger logger, const std::string& deps_dir)
    : BaseInference(model_path, model_name, ep_settings, logger, deps_dir),
      config_(logger),
      streamer_{std::move(std::make_shared<CallbackStreamer>())},
      generated_tokens_{0},
      generation_duration_ms_{0},
      first_token_latency_ms_{0} {}

void LLMInference::Init(const nlohmann::json& model_config,
                           std::optional<API_IHV_DeviceID_t> device_id) {
  if (error_message_ != "") {
    return;
  }
  if (!config_.LoadFromJson(model_config)) {
    error_message_ = "Failed to load model configuration";
    return;
  }

  std::filesystem::path file_path(model_path_);
  std::filesystem::path directory_path = file_path.parent_path();

  std::string device = device_id.has_value() ? devices_[device_id.value()] :
    default_device_;

  ov::AnyMap pipeline_config;
  if (device.find("NPU") != std::string::npos) {

    pipeline_config["MAX_PROMPT_LEN"] = static_cast<int>(
        config_.search.max_total_length - config_.search.max_length);
    if (config_.search.max_total_length > kMaxNpuAllocSize) {
      putenv("DISABLE_OPENVINO_GENAI_NPU_L0=1");
    }
    if ((config_.search.max_total_length <= kMaxNpuAllocSize) || (model_name_ != "llama2")) {
      pipeline_config["GENERATE_HINT"] = "BEST_PERF";
    }
  } else {
    ov::genai::SchedulerConfig scheduler_config;
    scheduler_config.max_num_batched_tokens = std::numeric_limits<size_t>::max();
    scheduler_config.enable_prefix_caching = false;
    pipeline_config[ov::genai::scheduler_config.name()] = scheduler_config;
  }

  try {
    logger_(LogLevel::kInfo,
        std::format("Creating pipeline on {} device", device));
    pipeline_.reset();  // Destroy old pipeline and free memory
    pipeline_.reset(new ov::genai::LLMPipeline(directory_path.string(),
                                               device,
                                               pipeline_config));
  } catch (const std::exception& e) {
    logger_(LogLevel::kError,
            std::format("Can't init GenAI pipeline: {}", e.what()));
    error_message_ = e.what();
  }

  logger_(LogLevel::kInfo,
          "IHV NativeOpenVINO GenAI inference pipeline initialized");
}

void LLMInference::Run(std::span<const uint32_t> input_data,
                          std::function<void(uint32_t)> token_callback) {
  if (input_data.size() + config_.search.max_length >
      config_.search.max_total_length) {
    error_message_ = "Input tokens + max_length exceeds max_total_length";
    logger_(LogLevel::kError, error_message_);
    logger_(LogLevel::kError,
            std::format("   Input tokens: {}", input_data.size()));
    logger_(LogLevel::kError,
            std::format("    + max_length {}", config_.search.max_length));
    logger_(LogLevel::kError, std::format("max_total_length {}",
                                          config_.search.max_total_length));
    return;
  }

  if (pipeline_ == nullptr) {
    error_message_ = "LLM pipeline not initialized";
    logger_(LogLevel::kError, error_message_);
    return;
  }

  try {
    // Create input tokens tensor
    const size_t len = input_data.size();
    constexpr size_t kBatchSize = 1;

    const ov::element::Type type{ov::element::i64};
    const ov::Shape shape{kBatchSize, len};

    // Convert tokens to 64-bit format
    std::vector<int64_t> input_data64;
    input_data64.reserve(len);
    for (int b = 0; b < kBatchSize; b++) {
      for (const auto v : input_data) {
        input_data64.emplace_back(static_cast<int64_t>(v));
      }
    }
    // Creates a tensor from the vector
    ov::Tensor input_tokens(type, shape, input_data64.data());

    ov::genai::GenerationConfig config;
    config.max_new_tokens = config_.search.max_length;

    streamer_->reset(config_.search.max_length, token_callback);

    // Generate tokens using the pipeline
    start_time_ = std::chrono::high_resolution_clock::now();
    encoded_results_ = pipeline_->generate(input_tokens, config, streamer_);
    end_time_ = std::chrono::high_resolution_clock::now();

    input_tokens_len_ = input_data.size();
    error_message_.clear();
  } catch (const std::exception& e) {
    error_message_ = e.what();
  }
}

void LLMInference::Prepare() {}

void LLMInference::Reset() {
  // Extract the generated token IDs from the results
  generated_tokens_ = 0;
  for (const auto& token_vec : encoded_results_.tokens) {
    generated_tokens_ += token_vec.size();
  }

  generation_duration_ms_ = GetDurationInMs(start_time_, end_time_);
  first_token_latency_ms_ =
      GetDurationInMs(start_time_, streamer_->GetTokenTimeOr(0, start_time_));
  const auto second_token_latency_ms =
      GetDurationInMs(streamer_->GetTokenTimeOr(0, start_time_),
                      streamer_->GetTokenTimeOr(1, start_time_));
  const auto average_token_rate =
      generated_tokens_ > 1
          ? (generation_duration_ms_ - first_token_latency_ms_) /
                static_cast<double>(generated_tokens_ - 1)
          : 0.0;
  const auto tokens_per_sec =
      static_cast<double>(generated_tokens_) / (generation_duration_ms_ / 1e3);

  logger_(LogLevel::kInfo, "----------------------------------");
  logger_(LogLevel::kInfo, std::format("Input tokens: {}", input_tokens_len_));
  logger_(LogLevel::kInfo, std::format("Generation Duration: (ms) {:.3f}",
                                       generation_duration_ms_));
  logger_(LogLevel::kInfo, "Single token latency:");
  logger_(LogLevel::kInfo,
          std::format(" - First Token: (ms) {:.3f}", first_token_latency_ms_));
  logger_(LogLevel::kInfo,
          std::format(" - Second Token: (ms) {:.3f}", second_token_latency_ms));
  logger_(LogLevel::kInfo,
          std::format("Average 2nd+ Token Latency: (ms) {:.3f} (+-{:.3f})",
                      average_token_rate, streamer_->GetMsCI(1, start_time_)));
  logger_(LogLevel::kInfo,
          std::format("Average 2nd+ Token Throughput: (token/sec) {:.3f}",
                      1000.0f / average_token_rate));
  logger_(LogLevel::kInfo,
          std::format("Total tokens generated: {}", generated_tokens_));
  logger_(LogLevel::kInfo, std::format("Tokens Per Second: (tokens/sec) {:.3f}",
                                       tokens_per_sec));
  logger_(LogLevel::kInfo, "----------------------------------");

  streamer_->reset();
}

void LLMInference::Deinit() {
  if (getenv("DISABLE_OPENVINO_GENAI_NPU_L0")) {
    putenv("DISABLE_OPENVINO_GENAI_NPU_L0=");
  }

  pipeline_.reset();  // Destroy pipeline object
  error_message_.clear();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
