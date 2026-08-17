#include "executor_factory.h"

#include <log4cxx/logger.h>

#include "benchmark/runner.h"
#include "executor_base.h"
#include "image/txt2img_executor.h"
#include "llm/txt2txt_executor.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerExecutorFactory(Logger::getLogger("ExecutorFactory"));

namespace cil {
namespace infer {

std::shared_ptr<ExecutorBase> ExecutorFactory::Create(
    const std::string& scenario_name, const std::string& model_base_name,
    const std::string& display_name, const std::string& model_path,
    std::shared_ptr<ScenarioDataProvider> data_provider,
    const std::string& library_path, const std::string& ep_name,
    const nlohmann::json& ep_config, const int iterations,
    const int iterations_warmup, const double inference_delay,
    const bool skip_failed_prompts, const bool is_agentic,
    const bool tools_execution) {
  if (BenchmarkRunner::IsLLMScenario(scenario_name)) {
    return std::make_shared<Txt2TxtExecutor>(
        scenario_name, model_base_name, display_name, model_path, data_provider,
        library_path, ep_name, ep_config, iterations, iterations_warmup,
        inference_delay, skip_failed_prompts, is_agentic, tools_execution);
  }

  if (BenchmarkRunner::IsImageScenario(scenario_name)) {
    return std::make_shared<Txt2ImgExecutor>(
        scenario_name, model_base_name, display_name, model_path, data_provider,
        library_path, ep_name, ep_config, iterations, iterations_warmup,
        inference_delay, skip_failed_prompts);
  }

  LOG4CXX_ERROR(loggerExecutorFactory,
                "Unknown executor type: " << scenario_name);
  return nullptr;
}

}  // namespace infer
}  // namespace cil
