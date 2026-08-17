/**
 * @file llm_executor.h
 *
 * @brief Abstract base class for LLM-domain executors (txt2txt, chat, ...).
 *
 * LLMExecutor owns all the plumbing that is common to any executor running
 * an LLM: model/tokenizer setup, path validation, progress/status tracking,
 * per-model logger, and the BenchmarkResult. Concrete scenarios implement
 * RunScenario() with their own inference loop.
 */
#ifndef LLM_EXECUTOR_H_
#define LLM_EXECUTOR_H_

#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "execution_provider.h"
#include "executor_base.h"
#include "tools/tokenizer.h"

namespace log4cxx {
class Logger;
using LoggerPtr = std::shared_ptr<Logger>;
}  // namespace log4cxx

namespace cil {

class ScenarioDataProvider;

namespace infer {

class LLMExecutor : public ExecutorBase {
 public:
  LLMExecutor(const std::string& scenario_name,
              const std::string& model_base_name,
              const std::string& display_name, const std::string& model_path,
              std::shared_ptr<ScenarioDataProvider> data_provider,
              const std::string& library_path, const std::string& ep_name,
              const nlohmann::json& ep_config, const int iterations,
              const int iterations_warmup, const double inference_delay,
              const bool skip_failed_prompts, const bool is_agentic,
              const bool tools_execution);

  virtual ~LLMExecutor() = default;

  BenchmarkResult GetBenchmarkResult() const override;
  bool Run() override;
  void Cancel() override;
  Status GetStatus() const override;
  int GetProgress() const override;
  int GetTotalSteps() const override;
  int GetCurrentStep() const override;
  std::chrono::high_resolution_clock::time_point GetStartTime() const override;
  std::string GetDescription() override;

 protected:
  /**
   * @brief Scenario-specific inference loop. Called by Run() after all
   * generic validation, tokenizer construction, and prompt counting have
   * succeeded. Implementations should use the protected state of this class
   * (tokenizer_, data_provider_, benchmark_result_, etc.) to run their
   * scenario and fill out benchmark_result_.
   */
  virtual void RunScenario(EP ep, const nlohmann::json& settings) = 0;

  /**
   * @brief Encode @p text into token ids using the executor's tokenizer.
   *
   * On failure, sets status_ to kFailed, fills benchmark_result_.error_message,
   * logs the error and returns false. An empty token list is treated as a
   * failure. The inference/model cleanup is the caller's responsibility.
   */
  bool TokenizeOrFail(std::string_view text, std::vector<uint32_t>& out_tokens,
                      bool add_special_tokens = true);

  /**
   * @brief Decode @p tokens back to text using the executor's tokenizer.
   *
   * Same error-reporting contract as TokenizeOrFail.
   */
  bool DetokenizeOrFail(std::span<const uint32_t> tokens,
                        std::string& out_text);

  std::string model_path_;
  std::vector<std::string> input_paths_;

  std::shared_ptr<ScenarioDataProvider> data_provider_;

  std::vector<std::vector<uint32_t>> expected_tokens_;

  std::atomic<Status> status_{Status::kReady};
  std::atomic<int> progress_{0};
  std::atomic<int> total_prompts_{0};
  std::atomic<int> current_prompt_{0};
  std::atomic<std::chrono::high_resolution_clock::time_point> start_time_;
  std::string task_description_;
  std::mutex task_description_mutex_;
  BenchmarkResult benchmark_result_;

  std::unique_ptr<cil::tools::Tokenizer> tokenizer_;

  const std::string scenario_name_;
  const std::string model_base_name_;
  const std::string display_name_;

  log4cxx::LoggerPtr executor_logger_;

  const bool skip_failed_required_prompts_;
  const bool is_agentic_;
  const bool tools_execution_;
};

}  // namespace infer
}  // namespace cil

#endif  // !LLM_EXECUTOR_H_
