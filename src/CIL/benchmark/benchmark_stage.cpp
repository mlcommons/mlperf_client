#include "benchmark_stage.h"

#include <set>

#include "../CLI/version.h"
#include "api_handler.h"
#include "executor_base.h"
#include "executor_factory.h"
#include "scenario_data_provider.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {
const std::chrono::milliseconds BenchmargStage::kProgressInterval =
    std::chrono::milliseconds(1000);

bool BenchmargStage::Run(const ScenarioConfig& scenario_config,
                         ScenarioData& scenario_data,
                         const ReportProgressCb& raport_progress_cb) {
  if (!scenario_data.output_results_file_paths.has_value()) {
    LOG4CXX_INFO(
        logger_,
        "Results Verification File is not set, verification is disabled");
  }

  auto scenario_data_providers = std::make_shared<ScenarioDataProvider>(
      scenario_data.asset_file_paths, scenario_data.input_file_paths,
      input_file_schema_path_,
      scenario_data.output_results_file_paths.value_or(""),
      output_results_schema_path_);

  const auto& execution_providers = scenario_data.prepared_eps;

  std::optional<std::string> last_model_name;
  std::string display_model_name;

  std::set<std::string> executed_models_hashes;

  std::vector<std::tuple<TaskScheduler, ProgressTracker,
                         std::vector<std::shared_ptr<infer::ExecutorBase>>>>
      benchmarks;

  bool task_executed = false;  // true if least 1 task is executed
  for (const auto& model_file_path : scenario_data.model_file_paths) {
    const std::string& model_source_path =
        scenario_data.path_to_source_map[model_file_path];
    ModelConfig model_config =
        scenario_config.GetModelByFilePath(model_source_path);

    const auto& model_name = model_config.GetName();

    std::string model_hash = utils::ComputeFileSHA256(model_file_path, logger_);
    if (!executed_models_hashes.empty() &&
        executed_models_hashes.contains(model_hash)) {
      LOG4CXX_INFO(logger_, "Model: " << model_source_path
                                      << " was already executed, skipping...");

      continue;
    }
    executed_models_hashes.insert(model_hash);

    unsigned new_executors_count = 0;

    std::stringstream eps_description_stream;
    for (auto& [ep, library_path] : execution_providers) {
      if (model_name.empty()) {
        // This is overriden scenario source path, check if the current EP
        // uses it
        if (!ep.HasModelFileConfig(model_source_path)) continue;

        display_model_name = ep.GetModelByFilePath(model_source_path).GetName();

      } else {
        if (ep.HasModelConfig(model_name)) {
          // This EP is overriding the scenario, so we skip its execution using
          // the global scenario
          continue;
        }
        display_model_name = model_name;
      }

      if (!last_model_name.has_value() ||
          last_model_name != display_model_name) {
        benchmarks.emplace_back(
            TaskScheduler("Benchmark Scheduler - " + scenario_config.GetName()),
            ProgressTracker(0, "benchmark", kProgressInterval),
            std::vector<std::shared_ptr<infer::ExecutorBase>>{});

        last_model_name = display_model_name;
      }

      auto& [scheduler, progress_tracker, executors] = benchmarks.back();

      nlohmann::json ep_config = ep.GetConfig();
      const auto& ep_name = ep.GetName();

      std::string tokenizer_path =
          ep.GetModelByFilePath(model_source_path).GetTokenizerPath();
      // if tokenizer_path is empty, it means this EP is not overriding it
      if (tokenizer_path.empty())
        tokenizer_path = model_config.GetTokenizerPath();

      scenario_data_providers->SetLLMTokenizerPath(
          scenario_data.source_to_path_map[tokenizer_path]);

      auto executor = infer::ExecutorFactory::Create(
          scenario_config.GetName(), model_file_path, scenario_data_providers,
          library_path, ep_name, ep_config, scenario_config.GetIterations(),
          scenario_config.GetIterationsWarmUp(),
          scenario_config.GetInferenceDelay(), skip_failed_prompts_);

      if (!executor) {
        LOG4CXX_ERROR(logger_, "Failed to run benchmark for "
                                   << scenario_config.GetName() << " model \""
                                   << display_model_name
                                   << "\", model file: " << model_file_path
                                   << ", EP: " << ep_name << ep_config);
        continue;
      }
      eps_description_stream
          << (eps_description_stream.str().empty() ? "" : ", ") << ep_name
          << ep_config;

      std::string task_name = "Benchmark " + ep_name;

      scheduler.ScheduleTask(task_name, [=, &task_executed]() {
        BenchmarkTask(ep_name, ep_config, model_source_path, display_model_name,
                      scenario_config, executor, task_executed);
      });
      progress_tracker.AddTask(executor);
      executors.push_back(executor);
      new_executors_count++;
    }

    auto original_name = scenario_data.path_to_source_map[model_file_path];

    if (new_executors_count)
      LOG4CXX_INFO(
          logger_,
          "Running benchmarks for EPs "
              << eps_description_stream.str() << " on model \""
              << (display_model_name.empty() ? model_name : display_model_name)
              << "\" (" << original_name << " -> " << model_file_path
              << "), added " << new_executors_count << " task(s)\n");
    else
      LOG4CXX_ERROR(logger_,
                    "No valid EPs found for model "
                        << (model_name.empty() ? "" : "\"" + model_name + "\" ")
                        << "(" << original_name << " -> " << model_file_path
                        << ")\n");
  }

  for (auto& [scheduler, progress_tracker, executors] : benchmarks) {
    if (!raport_progress_cb(progress_tracker, scheduler)) {
      for (auto executor : executors) executor->Cancel();
      LOG4CXX_INFO(logger_,
                   "\nModel " << scenario_config.GetName()
                              << ", Benchmark Stage interrupted, stopping...");

      return false;
    }
  }

  return task_executed;
}

void BenchmargStage::BenchmarkTask(
    const std::string& ep_name, const nlohmann::json& ep_config,
    const std::string& model_source_path, const std::string& display_model_name,
    const ScenarioConfig& scenario_config,
    std::shared_ptr<infer::ExecutorBase> executor, bool& task_executed) {
  LOG4CXX_INFO(logger_, "\nPreparing to run the benchmark for the EP "
                            << ep_name << ep_config);

  // MLPerf Power - "power_begin", "value": "02-25-2025 17:38:15.269"
  LOG4CXX_INFO(logger_, "power_begin - start " << scenario_config.GetName()
                                               << " " << ep_name << " "
                                               << ep_config);

  auto start_time = std::chrono::high_resolution_clock::now();

  (*executor)();
  task_executed = true;

  auto res = executor->GetBenchmarkResult();
  results_logger_.SetConfigVerified(
      skip_failed_prompts_ ? false : config_.IsConfigVerified());
  results_logger_.SetConfigExperimental(config_.IsConfigExperimental());
  results_logger_.SetApplicationVersionString(
      std::string(APP_VERSION_STRING) + " " + std::string(APP_BUILD_NAME));
  res.model_name = display_model_name;
  res.config_file_name = config_.GetConfigFileName();
  res.config_file_hash = config_.GetConfigFileHash();
  res.model_path = model_source_path;
  LOG4CXX_INFO(logger_, "Model " << scenario_config.GetName()
                                 << ", model path Result: " << res.model_path);
  res.asset_paths = scenario_config.GetAssets();
  res.data_paths = scenario_config.GetInputs();
  res.results_file = scenario_config.GetResultsFile();
  auto end_time = std::chrono::high_resolution_clock::now();
  res.benchmark_duration = utils::FormatDuration(end_time - start_time);

  // MLPerf Power - "power_end", "value": "02-25-2025 17:39:15.269"
  LOG4CXX_INFO(logger_, "power_end - end " << scenario_config.GetName() << " "
                                           << ep_name << " " << ep_config);

  try {
    bool passed = results_logger_.AppendBenchmarkResult(res);
    if (!passed) LOG4CXX_ERROR(logger_, "Failed to append benchmark result");
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(logger_, "Failed to append benchmark result: " << e.what());
  }
}

}  // namespace cil
