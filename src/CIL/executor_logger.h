/**
 * @file executor_logger.h
 *
 * @brief Per-executor log4cxx logger with file appender.
 *
 * Domain executors (LLMExecutor, ImageExecutor) create an instance of the
 * appropriate logger class and pass the LoggerPtr to ExecutorBase.
 * ExecutorLogger is for scenarios that run one model at a time.
 */
#ifndef EXECUTOR_LOGGER_H_
#define EXECUTOR_LOGGER_H_

#include <log4cxx/logger.h>

#include <string>

namespace cil::infer {

class ExecutorLogger {
 public:
  ExecutorLogger(const std::string& scenario_name,
                 const std::string& model_base_name);

  const log4cxx::LoggerPtr& Get() const { return logger_; }

  static std::string ResolveCanonicalScenarioName(const std::string& name);

 private:
  log4cxx::LoggerPtr logger_;
};

}  // namespace cil::infer

#endif  // EXECUTOR_LOGGER_H_
