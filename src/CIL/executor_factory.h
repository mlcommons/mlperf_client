/**
 * @file executor_factory.h
 * @brief The ExecutorFactory class used to create different executors based on
 * the type.
 *
 */
#ifndef EXECUTOR_FACTORY_H_
#define EXECUTOR_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

#include "execution_provider_config.h"

namespace cil {

class ScenarioDataProvider;

namespace infer {

class ExecutorBase;
/**
 * @class ExecutorFactory
 *
 * @brief The ExecutorFactory class is responsible for creating different
 * executors based on the type.
 *
 */
class ExecutorFactory {
 public:
  /**
   * @brief Create an executor based on the scenario name.
   *
   * @param scenario_name The scenario name (e.g. "txt2txt" or an alias).
   * @param model_base_name Machine-readable model identifier (e.g.
   * "phi_4_reasoning_14b").
   * @param display_name Human-readable model name for UI/logs.
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
   * @param skip_failed_prompts Whether to skip failed prompts during
   * benchmarking.
   * @param is_agentic Whether the scenario uses the agentic execution pipeline.
   * @param tools_execution Whether tools execution is enabled for the agentic
   * pipeline.
   * @return A shared pointer to the created executor.
   */
  static std::shared_ptr<ExecutorBase> Create(
      const std::string& scenario_name, const std::string& model_base_name,
      const std::string& display_name, const std::string& model_path,
      std::shared_ptr<ScenarioDataProvider> data_provider,
      const std::string& library_path, const std::string& ep_name,
      const nlohmann::json& ep_config, const int iterations,
      const int iterations_warmup, const double inference_delay,
      const bool skip_failed_prompts, const bool is_agentic,
      const bool tools_execution);
};

}  // namespace infer
}  // namespace cil

#endif  // !EXECUTOR_FACTORY_H_
