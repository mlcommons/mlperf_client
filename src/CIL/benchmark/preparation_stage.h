#ifndef PREPARATION_STAGE_H_
#define PREPARATION_STAGE_H_

#include "stage.h"

namespace cil {

class PreparationStage : public StageBase {
 public:
  PreparationStage(const log4cxx::LoggerPtr& logger,
                   const ExecutionConfig& config, Unpacker& unpacker,
                   EPDependenciesManager& ep_dependencies_manager,
                   bool enumerate_only, std::optional<int> device_id)
      : StageBase(logger, config, unpacker, ep_dependencies_manager),
        enumerate_only_(enumerate_only),
        device_id_(device_id) {}
  /**
   * @brief Prepares the configuration for the supplied scenarios.
   *
   * This method performs several tasks to prepare the application:
   * - Checks if the specified model is supported.
   * - Prepares the dependencies required by execution providers (eps).
   * - Loads the verification data for each scenario.
   *
   * @param scenario_config The scenario configuration to benchmark.
   * @param scenario_data The scenario runtime data to use for benchmarking.
   * @param raport_progress_cb The callback function to report the progress of
   * the benchmarking process.
   *
   * @return True if the preparation stage is successful, false otherwise.
   */
  bool Run(const ScenarioConfig& scenario_config, ScenarioData& scenario_data,
           const ReportProgressCb& raport_progress_cb) override;

  std::string GetName() const override { return "Preparation"; }

 private:
  class Impl;
  const bool enumerate_only_;
  const std::optional<int> device_id_;
};

}  // namespace cil

#endif  // !PREPARATION_STAGE_H_