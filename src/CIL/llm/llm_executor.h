/**
 * @file llama2_executor.h
 *
 * @brief The Llama2Executor class is responsible for running the Llama2
 * inference task.
 */
#ifndef LLAMA2_EXECUTOR_H_
#define LLAMA2_EXECUTOR_H_

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "execution_provider.h"
#include "executor_base.h"
#include "tokenizer/tfmtok.h"

namespace log4cxx { class Logger;
using LoggerPtr = std::shared_ptr<Logger>;
}  // namespace log4cxx

namespace cil {

class ScenarioDataProvider;

namespace infer {
/**
 * @class Llama2Executor
 *
 * @brief The Llama2Executor class is responsible for running the Llama2
 * inference task.
 */
class LLMExecutor : public ExecutorBase {
 public:
  enum class ModelType {
    Llama2,
    Llama3,
    Phi3_5,
    Phi4
  };
  /**
   * @brief Construct a new Llama2 Executor object
   *
   * @param model_path The path to the model file.
   * @param data_provider The data provider which provides the input data and
   * the other necessary data for the executor to run.
   * @param library_path The path to the library file.
   * @param ep_name The name of the execution provider.
   * @param ep_config The configuration of the execution provider.
   * @param iterations The number of iterations to run the executor.
   * @param iterations_warmup The number of warm-up iterations to run the
   * executor.
   * @param inference_delay The delay in seconds before calling inference.
   */
  LLMExecutor(ModelType model_type, const std::string& model_path,
              std::shared_ptr<ScenarioDataProvider> data_provider,
              const std::string& library_path, const std::string& ep_name,
              const nlohmann::json& ep_config, const int iterations,
              const int iterations_warmup, const double inference_delay,
              const bool skip_failed_prompts);

  virtual ~LLMExecutor() = default;

  /**
   * @brief Get the benchmark result.
   *
   * @return BenchmarkResult The benchmark result.
   */
  BenchmarkResult GetBenchmarkResult() const override;

  /**
   * @brief Run the Llama2 inference task.
   *
   * This method is responsible for running the Llama2 inference task and it is
   * called by the TaskScheduler.
   *
   * @return true If the task is successfully run, false otherwise.
   */
  bool Run() override;
  /**
   * @brief Cancel the Llama2 inference task.
   *
   * This method is responsible for canceling the Llama2 inference task and it
   * is called by the TaskScheduler. The will not be canceled immediately, it
   * will be canceled after the current processed prompt is finished.
   *
   */
  void Cancel() override;
  /**
   * @brief Get the status of the Llama2 inference task.
   *
   * @return Status The status of the Llama2 inference task (e.g. Running,
   * Finished, Canceled).
   */
  Status GetStatus() const override;
  /**
   * @brief Get the progress of the Llama2 inference task.
   *
   * @return int The progress percentage of the Llama2 inference task.
   */
  int GetProgress() const override;
  /**
   * @brief Get the total number of prompts to process.
   *
   * @return int Total number of prompts.
   */
  int GetTotalSteps() const override;
  /**
   * @brief Get the number of prompts processed so far.
   *
   * @return int Number of prompts processed so far.
   */
  int GetCurrentStep() const override;
  /**
   * @brief Get the start time of the Llama2 inference task.
   *
   * @return std::chrono::high_resolution_clock::time_point The start time of
   * the Llama2 inference task.
   */
  std::chrono::high_resolution_clock::time_point GetStartTime() const override;
  /**
   * @brief Get the description of the Llama2 inference task.
   *
   * @return std::string The description of the Llama2 inference task which will
   * be displayed in the CLI.
   */
  std::string GetDescription() override;

 protected:
  void RunLLMTask(EP ep, const nlohmann::json& settings);

  std::string model_path_;
  std::vector<std::string> input_paths_;

  std::shared_ptr<ScenarioDataProvider> data_provider_;

  std::vector<std::vector<uint32_t>> expected_tokens_;

  std::atomic<Status> status_;
  std::atomic<int> progress_;
  std::atomic<int> total_prompts_;
  std::atomic<int> current_prompt_;
  std::atomic<std::chrono::high_resolution_clock::time_point> start_time_;
  std::string task_description_;
  std::mutex task_description_mutex_;
  BenchmarkResult benchmark_result_;

  std::unique_ptr<tfm::Tokenizer> tokenizer_;

  const ModelType model_type_;

  log4cxx::LoggerPtr& llm_executor_logger_;

  const bool skip_failed_prompts_;
};

class Llama2Executor : public LLMExecutor {
 public:
  Llama2Executor(const std::string& model_path,
                 std::shared_ptr<ScenarioDataProvider> data_provider,
                 const std::string& library_path, const std::string& ep_name,
                 const nlohmann::json& ep_config, const int iterations,
                 const int iterations_warmup, const double inference_delay,
                 const bool skip_failed_prompts)
      : LLMExecutor(ModelType::Llama2, model_path, data_provider, library_path,
                    ep_name, ep_config, iterations, iterations_warmup,
                    inference_delay, skip_failed_prompts) {}
  virtual ~Llama2Executor() = default;
};

class Llama3Executor : public LLMExecutor {
 public:
  Llama3Executor(const std::string& model_path,
                 std::shared_ptr<ScenarioDataProvider> data_provider,
                 const std::string& library_path, const std::string& ep_name,
                 const nlohmann::json& ep_config, const int iterations,
                 const int iterations_warmup, const double inference_delay,
                 const bool skip_failed_prompts)
      : LLMExecutor(ModelType::Llama3, model_path, data_provider, library_path,
                    ep_name, ep_config, iterations, iterations_warmup,
                    inference_delay, skip_failed_prompts) {}
  virtual ~Llama3Executor() = default;
};

class Phi3_5Executor : public LLMExecutor {
 public:
  Phi3_5Executor(const std::string& model_path,
                 std::shared_ptr<ScenarioDataProvider> data_provider,
                 const std::string& library_path, const std::string& ep_name,
                 const nlohmann::json& ep_config, const int iterations,
                 const int iterations_warmup, const double inference_delay,
                 const bool skip_failed_prompts)
      : LLMExecutor(ModelType::Phi3_5, model_path, data_provider, library_path,
                    ep_name, ep_config, iterations, iterations_warmup,
                    inference_delay, skip_failed_prompts) {}
  virtual ~Phi3_5Executor() = default;
};

class Phi4Executor : public LLMExecutor {
 public:
  Phi4Executor(const std::string& model_path,
                 std::shared_ptr<ScenarioDataProvider> data_provider,
                 const std::string& library_path, const std::string& ep_name,
                 const nlohmann::json& ep_config, const int iterations,
                 const int iterations_warmup, const double inference_delay,
                 const bool skip_failed_prompts)
      : LLMExecutor(ModelType::Phi4, model_path, data_provider, library_path,
                    ep_name, ep_config, iterations, iterations_warmup,
                    inference_delay, skip_failed_prompts) {}
  virtual ~Phi4Executor() = default;
};

}  // namespace infer
}  // namespace cil

#endif  // !LLAMA2_EXECUTOR_H_
