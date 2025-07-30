#ifndef BENCHMARK_STAGE_H_
#define BENCHMARK_STAGE_H_

#include "benchmark_logger.h"
#include "stage.h"

namespace cil {
namespace infer {
class ExecutorBase;
}

class BenchmargStage : public StageBase {
 public:
  BenchmargStage(const log4cxx::LoggerPtr& logger,
                 const ExecutionConfig& config, Unpacker& unpacker,
                 EPDependenciesManager& ep_dependencies_manager,
                 BenchmarkLogger& results_logger,
                 const std::string& output_results_schema_path,
                 const std::string& input_file_schema_path,
                 bool skip_failed_prompts)
      : StageBase(logger, config, unpacker, ep_dependencies_manager),
        results_logger_(results_logger),
        output_results_schema_path_(output_results_schema_path),
        input_file_schema_path_(input_file_schema_path),
        skip_failed_prompts_(skip_failed_prompts) {}

  /**
   * @brief Benchmarks the specified scenarios.
   *
   * This method benchmarks the specified scenarios using the provided execution
   * providers (eps) and displays the results, also logs the result to
   * logs/results.json. the benchmarking process includes the following steps:
   * - Loading the model and input files.
   * - Running the benchmarking process.
   * - collecting system information.
   * - Logging the results.
   *
   * @param scenario_config The scenario configuration to benchmark.
   * @param scenario_data The scenario runtime data to use for benchmarking.
   * @param raport_progress_cb The callback function to report the progress of
   * the benchmarking process.
   *
   * @return True if the benchmark stage is successful, false otherwise.
   */
  bool Run(const ScenarioConfig& scenario_config, ScenarioData& scenario_data,
           const ReportProgressCb& raport_progress_cb) override;

  std::string GetName() const override { return "Benchmark"; }

 private:
  void BenchmarkTask(const std::string& ep_name,
                     const nlohmann::json& ep_config,
                     const std::string& model_source_path,
                     const std::string& display_model_name,
                     const ScenarioConfig& scenario_config,
                     std::shared_ptr<infer::ExecutorBase> executor,
                     bool& task_executed);

  BenchmarkLogger& results_logger_;

  const std::string& output_results_schema_path_;
  const std::string& input_file_schema_path_;
  const bool skip_failed_prompts_;

  static const std::chrono::milliseconds kProgressInterval;
};

}  // namespace cil

#endif