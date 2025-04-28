#ifndef STAGE_H_
#define STAGE_H_

#include <log4cxx/logger.h>

#include <chrono>

#include "ep_dependencies_manager.h"
#include "execution_config.h"
#include "progress_tracker.h"
#include "scenario_data.h"
#include "task_scheduler.h"
#include "unpacker.h"

namespace cil {

class StageBase {
 public:
  using ReportProgressCb = std::function<bool(ProgressTracker& progress_tracker,
                                              TaskScheduler& task_scheduler)>;

  StageBase(const log4cxx::LoggerPtr& logger, const ExecutionConfig& config,
            Unpacker& unpacker, EPDependenciesManager& ep_dependencies_manager)
      : logger_(logger),
        config_(config),
        unpacker_(unpacker),
        ep_dependencies_manager_(ep_dependencies_manager) {}

  virtual ~StageBase() = default;

  virtual bool Run(const ScenarioConfig& scenario_config,
                   ScenarioData& scenario_data,
                   const ReportProgressCb& raport_progress_cb) = 0;

  virtual std::string GetName() const = 0;


  const log4cxx::LoggerPtr& GetLogger() const { return logger_; }
  const ExecutionConfig& GetConfig() const { return config_; }
  Unpacker& GetUnpacker() { return unpacker_; }
  EPDependenciesManager& GetEPDependenciesManager() {
    return ep_dependencies_manager_;
  }

 protected:
  const log4cxx::LoggerPtr& logger_;
  const ExecutionConfig& config_;
  Unpacker& unpacker_;
  EPDependenciesManager& ep_dependencies_manager_;
};

using StagePtr = std::shared_ptr<StageBase>;

class CustomStage : public StageBase {
 public:
  using RunFunc = std::function<bool(const ScenarioConfig&, ScenarioData&,
                                     const ReportProgressCb&)>;

  CustomStage(const log4cxx::LoggerPtr& logger, const ExecutionConfig& config,
              Unpacker& unpacker,
              EPDependenciesManager& ep_dependencies_manager,
              const std::string& name, const RunFunc& run_func)
      : StageBase(logger, config, unpacker, ep_dependencies_manager),
        name_(name),
        run_func_(run_func) {}

  bool Run(const ScenarioConfig& scenario_config, ScenarioData& scenario_data,
           const ReportProgressCb& raport_progress_cb) override {
    return run_func_(scenario_config, scenario_data, raport_progress_cb);
  }

  std::string GetName() const override { return name_; }

 private:
  const std::string name_;
  const RunFunc run_func_;
};

}  // namespace cil

#endif  // !STAGE_H_