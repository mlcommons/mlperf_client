#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <openvino/genai/streamer_base.hpp>
#include <openvino/genai/tokenizer.hpp>

#include "../../IHV.h"  // Include IHV API definitions
#include "../base_inference.h"
#include "Llama2/llama_config.h"

namespace {

static double GetDurationInMs(
    std::chrono::high_resolution_clock::time_point start,
    std::chrono::high_resolution_clock::time_point end) {
  return static_cast<double>(
             std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                 .count()) /
         1e6;
}

class CallbackStreamer final : public ov::genai::StreamerBase {
 public:
  using clock = std::chrono::high_resolution_clock;
  using time_point = CallbackStreamer::clock::time_point;

  CallbackStreamer() = default;

  bool put(int64_t token) override {
    token_callback_(static_cast<uint32_t>(token));
    token_time_.emplace_back(clock::now());  // Record token generation time
    return false;                            // Continue tokens generation
  }

  void end() override {}

  void reset(size_t max_tokens = 0,
             std::function<void(uint32_t)> token_callback = 0) {
    token_callback_ = token_callback;
    token_time_.clear();
    if (max_tokens > 0) {
      token_time_.reserve(max_tokens);
    }
  }

  CallbackStreamer::time_point GetTokenTimeOr(
      size_t idx, CallbackStreamer::time_point def) const {
    return idx < token_time_.size() ? token_time_[idx] : def;
  }

  double GetMsCI(size_t start_token,
                 CallbackStreamer::time_point start_time) const;

 private:
  std::vector<CallbackStreamer::time_point> token_time_;
  std::function<void(uint32_t)> token_callback_;
};

}  // namespace

namespace cil {
namespace IHV {
namespace infer {

class Llama2Inference : public BaseInference {
 public:
  Llama2Inference(const std::string& model_path,
                  const NativeOpenVINOExecutionProviderSettings& ep_settings,
                  cil::Logger logger, const std::string& deps_dir);

  void Init(const nlohmann::json& model_config);

  void Prepare() override;

  void Run(std::span<const uint32_t> input_data,
           std::function<void(uint32_t)> token_callback);

  void Reset() override;

  void Deinit() override;

  double GetTimeToFirstTokenSec() const {
    return first_token_latency_ms_ / 1e3;
  }

  size_t GetGeneratedTokensCount() const { return generated_tokens_; }

  double GetGenerationDurationSec() const {
    return generation_duration_ms_ / 1e3;
  }

 private:
  cil::infer::LlamaConfig config_;
  std::shared_ptr<CallbackStreamer> streamer_;
  std::unique_ptr<ov::genai::LLMPipeline> pipeline_;
  std::vector<uint32_t> output_data_;

  // Benchmark timers
  size_t input_tokens_len_;
  size_t generated_tokens_;
  double first_token_latency_ms_;
  double generation_duration_ms_;

  CallbackStreamer::time_point start_time_;
  CallbackStreamer::time_point end_time_;
  ov::genai::EncodedResults encoded_results_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
