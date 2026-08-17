#include "executor_logger.h"

#include <log4cxx/fileappender.h>
#include <log4cxx/helpers/object.h>
#include <log4cxx/helpers/pool.h>
#include <log4cxx/level.h>
#include <log4cxx/patternlayout.h>

#include <filesystem>

#include "benchmark/runner.h"
#include "utils.h"

namespace fs = std::filesystem;

namespace cil::infer {

namespace {
fs::path ResolveLogsDir() {
  auto results = log4cxx::Logger::getLogger(LOG4CXX_STR("Results"));
  if (auto file_appender = log4cxx::cast<log4cxx::FileAppender>(
          results->getAppender(LOG4CXX_STR("ResultsFileAppender")))) {
    if (fs::path results_file = file_appender->getFile();
        results_file.is_absolute()) {
      return results_file.parent_path();
    }
  }
  return utils::GetAppDefaultDataPath() / "Logs";
}
}  // namespace

std::string ExecutorLogger::ResolveCanonicalScenarioName(
    const std::string& name) {
  if (BenchmarkRunner::IsLLMScenario(name)) return "txt2txt";
  if (BenchmarkRunner::IsImageScenario(name)) return "txt2img";
  return name;
}

ExecutorLogger::ExecutorLogger(const std::string& scenario_name,
                               const std::string& model_base_name) {
  const std::string canonical = ResolveCanonicalScenarioName(scenario_name);

  const fs::path logs_dir = ResolveLogsDir();

  std::string logger_name;
  fs::path log_path;
  if (scenario_name == canonical) {
    logger_name = canonical + "." + model_base_name;
    log_path = logs_dir / canonical / (model_base_name + ".log");
  } else {
    std::string alias = utils::StringToLowerCase(
        utils::StringReplaceChar(scenario_name, '.', '_'));
    logger_name = alias;
    log_path = logs_dir / (alias + "_executor.log");
  }

  logger_ = log4cxx::Logger::getLogger(logger_name);

  if (logger_->getAllAppenders().empty()) {
    std::error_code ec;
    fs::create_directories(log_path.parent_path(), ec);

    auto layout = std::make_shared<log4cxx::PatternLayout>(
        LOG4CXX_STR("[%d{MM-dd-yyyy HH:mm:ss.SSS}] %c %-5p - %m%n"));
    auto appender = std::make_shared<log4cxx::FileAppender>(
        layout, log_path.string(), true);
    log4cxx::helpers::Pool pool;
    appender->activateOptions(pool);
    logger_->addAppender(appender);

    auto root = log4cxx::Logger::getRootLogger();
    if (auto error_appender =
            root->getAppender(LOG4CXX_STR("ErrorFileAppender"))) {
      logger_->addAppender(error_appender);
    }

    logger_->setLevel(log4cxx::Level::getAll());
    logger_->setAdditivity(false);
  }
}

}  // namespace cil::infer
