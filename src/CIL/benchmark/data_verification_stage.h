#ifndef DATA_VERIFICATION_STAGE_H_
#define DATA_VERIFICATION_STAGE_H_

#include "stage.h"

namespace cil {

class DataVerificationStage : public StageBase {
 public:
  DataVerificationStage(const log4cxx::LoggerPtr& logger,
                        const ExecutionConfig& config, Unpacker& unpacker,
                        EPDependenciesManager& ep_dependencies_manager,
                        const std::string& data_verification_file_schema_path)
      : StageBase(logger, config, unpacker, ep_dependencies_manager),
        data_verification_file_schema_path_(
            data_verification_file_schema_path) {}

  /**
   * @brief Verifies the data for the specified scenarios.
   *
   * This method verifies the data for the specified scenarios, including all
   * the downloaded files. It checks the file signatures and hashes against the
   * provided data verification file for each scenario to ensure the integrity
   * of the downloaded files.
   *
   * @param scenario_config The scenario configuration to benchmark.
   * @param scenario_data The scenario runtime data to use for benchmarking.
   * @param raport_progress_cb The callback function to report the progress of
   * the benchmarking process.
   *
   * @return True if the data verification stage is successful, false otherwise.
   */
  bool Run(const ScenarioConfig& scenario_config, ScenarioData& scenario_data,
           const ReportProgressCb& raport_progress_cb) override;

  std::string GetName() const override { return "DataVerification"; }

 private:
  const std::string& data_verification_file_schema_path_;
};

}  // namespace cil

#endif