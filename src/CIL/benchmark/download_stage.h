#ifndef DOWNLOAD_STAGE_H_
#define DOWNLOAD_STAGE_H_

#include "stage.h"

namespace cil {

class DownloadStage : public StageBase {
 public:
  DownloadStage(const log4cxx::LoggerPtr& logger, const ExecutionConfig& config,
                Unpacker& unpacker,
                EPDependenciesManager& ep_dependencies_manager,
                const std::string& data_dir)
      : StageBase(logger, config, unpacker, ep_dependencies_manager),
        data_dir_(data_dir) {}

  /**
   * @brief Downloads the necessary files for specified scenarios indexed.
   *
   * This method downloads the necessary files for the specified scenarios,
   * including the model, input, and asset files. Additionally, it downloads the
   * necessary dependencies for the execution providers (eps). This stage is
   * also responsible for unpacking the downloaded files and preparing them for
   * the next stages.
   *
   * @param scenario_config The scenario configuration to benchmark.
   * @param scenario_data The scenario runtime data to use for benchmarking.
   * @param raport_progress_cb The callback function to report the progress of
   * the benchmarking process.
   *
   * @return True if the download stage is successful, false otherwise.
   */
  bool Run(const ScenarioConfig& scenario_config, ScenarioData& scenario_data,
           const ReportProgressCb& raport_progress_cb) override;

  std::string GetName() const override { return "Download"; }

 private:
  const std::string data_dir_;

  static const std::chrono::milliseconds kProgressInterval;
};

}  // namespace cil

#endif