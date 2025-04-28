#ifndef BENCHMARK_RUNNER_H
#define BENCHMARK_RUNNER_H

#include <string_view>

#include "stage.h"

namespace cil {
class BenchmarkLogger;

/**
 * @class BenchmarkRunner
 *
 * @brief The BenchmarkRunner class is responsible for orchestrating the various
 * stages of the run, such a preparation, download data etc.
 * verification, benchmarking
 */
class BenchmarkRunner {
 public:
  struct Params {
    log4cxx::LoggerPtr logger;          // must not be null
    ExecutionConfig* config = nullptr;  // must not be null
    Unpacker* unpacker = nullptr;       // must not be null
    EPDependenciesManager* ep_dependencies_manager =
        nullptr;  // must not be null
    ProgressTracker::HandlerBase* progress_handler = nullptr;

    std::string output_dir;  // needed for logging results to file
    std::string data_dir;    // needed for downloading files

    bool enumerate_only = false;  // enumerate devices too during preparation
    std::optional<int> device_id;  // override device id during preparation

    // Needed for preparation stage, must not be empty
    // These paths are used to locate the schemas for various validation during
    // preparation step
    std::string output_results_schema_path;
    std::string data_verification_file_schema_path;
    std::string input_file_schema_path;
  };

  /**
   * @brief Constructor for BenchmarkRunner class.
   *
   * @param params Parameters for the BenchmarkRunner.
   */
  explicit BenchmarkRunner(Params& params);
  ~BenchmarkRunner();

  BenchmarkLogger& GetResultsLogger() const;

  using OnEnterStageCb = std::function<bool(const std::string_view&, int)>;
  using OnFailedStageCb = std::function<bool(const std::string_view&, int)>;

  void SetOnFailedStageCallback(const OnFailedStageCb& on_failed_stage);
  void SetOnEnterStageCallback(const OnEnterStageCb& on_enter_stage);

  void AddCustomStage(const std::string& name,
                      const CustomStage::RunFunc& stage_function,
                      const std::string& last_stage = "");

  void AddCustomStage(std::shared_ptr<StageBase> stage,
                      const std::string& last_stage = "");

  // We don't need remove stage functions explicitly, as the stages are
  // meant to be added in a specific order and not removed.

  /**
   * @brief Runs the benchmark stages.
   *
   * This method is responsible for running the benchmark stages, including
   * download, preparation, data verification, benchmarking, and
   * any custom stage defined by the user
   *
   * @return True if the benchmark run is successful, false otherwise.
   */
  bool Run();

 private:
  bool ReportProgress(ProgressTracker& progress_tracker,
                      TaskScheduler& task_scheduler);

  ProgressTracker::HandlerBase* progress_handler_;
  ExecutionConfig* config_;
  Unpacker* unpacker_;
  EPDependenciesManager* ep_dependencies_manager_;
  std::unique_ptr<BenchmarkLogger> results_logger_;

  const log4cxx::LoggerPtr& logger_;

  std::map<std::string, std::shared_ptr<StageBase>> stages_;
  std::vector<std::string> stage_order_;

  OnFailedStageCb on_failed_stage_;
  OnEnterStageCb on_enter_stage_;
};

}  // namespace cil

#endif