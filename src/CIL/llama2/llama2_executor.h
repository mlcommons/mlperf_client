/**
 * @file llama2_executor.h
 * 
 * @brief The Llama2Executor class is responsible for running the Llama2 inference task.
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

namespace cil {

class ScenarioDataProvider;

namespace infer {
/**
 * @class Llama2Executor
 * 
 * @brief The Llama2Executor class is responsible for running the Llama2 inference task.
*/
class Llama2Executor : public ExecutorBase {
 public:
  /**
   * @brief Construct a new Llama2 Executor object
   * 
   * @param model_path The path to the model file.
   * @param data_provider The data provider which provides the input data and the other necessary data for the executor to run.
   * @param library_path The path to the library file.
   * @param ep_name The name of the execution provider.
   * @param ep_config The configuration of the execution provider.
   * @param iterations The number of iterations to run the executor.
   * @param iterations_warmup The number of warm-up iterations to run the executor.
   * @param inference_delay The delay in seconds before calling inference.
   */
  Llama2Executor(const std::string& model_path,
                 std::shared_ptr<ScenarioDataProvider> data_provider,
                 const std::string& library_path, const std::string& ep_name,
                 const nlohmann::json& ep_config, const int iterations,
                 const int iterations_warmup, const double inference_delay);

  /**
   * @brief Get the benchmark result.
   * 
   * @return BenchmarkResult The benchmark result.
  */
  BenchmarkResult GetBenchmarkResult() const override;

  /**
   * @brief Run the Llama2 inference task.
   * 
   * This method is responsible for running the Llama2 inference task and it is called by the TaskScheduler.
   * 
   * @return true If the task is successfully run, false otherwise.
  */
  bool Run() override;
  /**
   * @brief Cancel the Llama2 inference task.
   * 
   * This method is responsible for canceling the Llama2 inference task and it is called by the TaskScheduler.
   * The will not be canceled immediately, it will be canceled after the current processed prompt is finished.
   * 
  */
  void Cancel() override;
  /**
   * @brief Get the status of the Llama2 inference task.
   * 
   * @return Status The status of the Llama2 inference task (e.g. Running, Finished, Canceled).
  */
  Status GetStatus() const override;
  /**
   * @brief Get the progress of the Llama2 inference task.
   * 
   * @return int The progress percentage of the Llama2 inference task.
  */
  int GetProgress() const override;
  /**
   * @brief Get the start time of the Llama2 inference task.
   * 
   * @return std::chrono::high_resolution_clock::time_point The start time of the Llama2 inference task.
  */
  std::chrono::high_resolution_clock::time_point GetStartTime() const override;
  /**
   * @brief Get the description of the Llama2 inference task.
   * 
   * @return std::string The description of the Llama2 inference task which will be displayed in the CLI.
  */
  std::string GetDescription() override;

 private:
  void RunLlama2Task(EP ep, const nlohmann::json& settings);

  std::string model_path_;
  std::vector<std::string> input_paths_;

  std::shared_ptr<ScenarioDataProvider> data_provider_;

  std::vector<std::vector<uint32_t>> expected_tokens_;

  std::atomic<Status> status_;
  std::atomic<int> progress_;
  std::atomic<std::chrono::high_resolution_clock::time_point> start_time_;
  std::string task_description_;
  std::mutex task_description_mutex_;
  BenchmarkResult benchmark_result_;

  std::unique_ptr<tfm::Tokenizer> tokenizer_;
  int total_prompts_;
};

}  // namespace infer
}  // namespace cil

#endif  // !LLAMA2_EXECUTOR_H_
