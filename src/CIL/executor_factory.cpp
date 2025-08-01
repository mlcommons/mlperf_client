#include "executor_factory.h"

#include <log4cxx/logger.h>

#include "executor_base.h"
#include "llm/llm_executor.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerExecutorFactory(Logger::getLogger("ExecutorFactory"));

namespace cil {
namespace infer {

std::shared_ptr<ExecutorBase> ExecutorFactory::Create(
    const std::string& type, const std::string& model_path,
    std::shared_ptr<ScenarioDataProvider> data_provider,
    const std::string& library_path, const std::string& ep_name,
    const nlohmann::json& ep_config, const int iterations,
    const int iterations_warmup, const double inference_delay,
    const bool skip_failed_prompts) {
  std::string lower_case_type = utils::StringToLowerCase(type);
  if (lower_case_type == "llama2")
    return std::make_shared<Llama2Executor>(
        model_path, data_provider, library_path, ep_name, ep_config, iterations,
        iterations_warmup, inference_delay, skip_failed_prompts);
  else if (lower_case_type == "llama3")
    return std::make_shared<Llama3Executor>(
        model_path, data_provider, library_path, ep_name, ep_config, iterations,
        iterations_warmup, inference_delay, skip_failed_prompts);
  else if (lower_case_type == "phi3.5")
    return std::make_shared<Phi3_5Executor>(
        model_path, data_provider, library_path, ep_name, ep_config, iterations,
        iterations_warmup, inference_delay, skip_failed_prompts);
  else if (lower_case_type == "phi4")
    return std::make_shared<Phi4Executor>(
        model_path, data_provider, library_path, ep_name, ep_config, iterations,
        iterations_warmup, inference_delay, skip_failed_prompts);

  LOG4CXX_ERROR(loggerExecutorFactory, "Unknown executor type: " << type);
  return nullptr;
}

}  // namespace infer
}  // namespace cil
