#include "runner.h"

#include "benchmark_stage.h"
#include "data_verification_stage.h"
#include "download_stage.h"
#include "preparation_stage.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;

namespace {

const std::unordered_set<std::string> kSupportedLLMModels {
    "llama2", "llama3", "phi3.5", "phi4"
  };

} // namespace

const std::unordered_map<std::string, std::string> kModelFullNames = {
    {"llama2", "Llama 2 7B Chat"},
    {"llama3", "Llama 3.1 8B Instruct"},
    {"phi3.5", "Phi 3.5 Mini Instruct"},
    {"phi4", "Phi 4 Reasoning 14B"}};

namespace cil {

BenchmarkRunner::BenchmarkRunner(Params& params)
    : progress_handler_(params.progress_handler),
      config_(params.config),
      unpacker_(params.unpacker),
      ep_dependencies_manager_(params.ep_dependencies_manager),
      results_logger_(std::make_unique<BenchmarkLogger>(params.output_dir)),
      logger_(params.logger),
      enumerate_only_(params.enumerate_only) {
  if (!logger_) {
    throw std::invalid_argument("Logger is not provided.");
  }

  if (!config_) {
    throw std::invalid_argument("ExecutionConfig is not provided.");
  }

  if (!unpacker_) {
    throw std::invalid_argument("Unpacker is not provided.");
  }

  if (!ep_dependencies_manager_) {
    throw std::invalid_argument("EPDependenciesManager is not provided.");
  }

  if (params.enumerate_only) {
    config_->GetSystemConfig().SetDownloadBehavior("deps_only_enumeration");
  }

  auto download_stage = std::make_shared<DownloadStage>(
      logger_, *config_, *unpacker_, *ep_dependencies_manager_,
      params.data_dir);
  stages_[download_stage->GetName()] = download_stage;

  auto preparation_stage = std::make_shared<PreparationStage>(
      logger_, *config_, *unpacker_, *ep_dependencies_manager_,
      params.enumerate_only, params.device_id);
  stages_[preparation_stage->GetName()] = preparation_stage;

  if (!params.enumerate_only) {
    auto data_verification_stage = std::make_shared<DataVerificationStage>(
        logger_, *config_, *unpacker_, *ep_dependencies_manager_,
        params.data_verification_file_schema_path);
    stages_[data_verification_stage->GetName()] = data_verification_stage;

    auto benchmark_stage = std::make_shared<BenchmargStage>(
        logger_, *config_, *unpacker_, *ep_dependencies_manager_,
        *results_logger_, params.output_results_schema_path,
        params.input_file_schema_path, params.skip_failed_prompts);
    stages_[benchmark_stage->GetName()] = benchmark_stage;
  }

  stage_order_ = {"Download", "Preparation", "DataVerification", "Benchmark"};

  on_enter_stage_ = [](const std::string_view&, int) { return true; };
  on_failed_stage_ = [](const std::string_view&, int) { return false; };
}

const std::unordered_set<std::string> BenchmarkRunner::kSupportedModels = {
    "llama2", "llama3", "phi3.5", "phi4"};

bool BenchmarkRunner::IsSupportedModel(const std::string& model_name) {
  const auto lower_model_name = utils::StringToLowerCase(model_name);
  return kSupportedModels.contains(lower_model_name);
}

bool BenchmarkRunner::IsLLMModel(const std::string& model_name) {
  const auto lower_model_name = utils::StringToLowerCase(model_name);
  return kSupportedLLMModels.contains(lower_model_name);
}

void BenchmarkRunner::ClearCache(const std::string& deps_dir) {
  DownloadStage::ClearCache(deps_dir);
}

std::vector<BenchmarkRunner::PreparedEPList> BenchmarkRunner::GetPreparedEPs()
    const {
  return prepared_eps_;
}

std::optional<std::string> BenchmarkRunner::GetModelFullName(
    const std::string& model_name) {
  const auto it = kModelFullNames.find(model_name);
  if (it != kModelFullNames.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool BenchmarkRunner::ReportProgress(ProgressTracker& progress_tracker,
                                     TaskScheduler& task_scheduler) {
  if (progress_handler_)
    progress_handler_->StartTracking(progress_tracker);
  else
    progress_tracker.StartTracking();

  task_scheduler.ExecuteTasksAsync();

  auto update_internal = progress_tracker.GetUpdateInterval();

  if (progress_handler_) {
    auto& interrupt = progress_handler_->GetInterrupt();

    while (!progress_tracker.Finished()) {
      progress_handler_->Update(progress_tracker);
      std::this_thread::sleep_for(update_internal);
      if (interrupt) {
        if (!progress_handler_->RequestInterrupt(progress_tracker)) {
          interrupt = false;
          continue;
        }
        task_scheduler.CancelTasks();
        if (progress_handler_)
          progress_handler_->StopTracking(progress_tracker);
        else
          progress_tracker.StopTracking();
        return false;
      }
    }
  } else {
    while (!progress_tracker.Finished()) {
      std::this_thread::sleep_for(update_internal);
    }
  }

  task_scheduler.Join();
  if (progress_handler_)
    progress_handler_->StopTracking(progress_tracker);
  else
    progress_tracker.StopTracking();

  return true;
}

BenchmarkLogger& BenchmarkRunner::GetResultsLogger() const {
  return *results_logger_.get();
}

void BenchmarkRunner::SetOnFailedStageCallback(
    const OnFailedStageCb& on_failed_stage) {
  on_failed_stage_ = on_failed_stage;
}

void BenchmarkRunner::SetOnEnterStageCallback(
    const OnEnterStageCb& on_enter_stage) {
  on_enter_stage_ = on_enter_stage;
}

bool BenchmarkRunner::Run() {
  const auto& scenarios = config_->GetScenarios();

  auto progress_reporter = [this](ProgressTracker& progress_tracker,
                                  TaskScheduler& task_scheduler) {
    return ReportProgress(progress_tracker, task_scheduler);
  };

  bool was_error = false;

  for (int i = 0; i < scenarios.size(); ++i) {
    const auto& scenario = scenarios[i];

    ScenarioData scenario_data;

    for (const auto& stage_name : stage_order_) {
      if (!on_enter_stage_(stage_name, i) ||
          !stages_[stage_name]->Run(scenario, scenario_data,
                                    progress_reporter) &&
              !on_failed_stage_(stage_name, i)) {
        was_error = true;
        break;
      }
    }
    if (enumerate_only_) prepared_eps_.push_back(scenario_data.prepared_eps);
  }

  return !was_error;
}

int BenchmarkRunner::GetTotalStages() const { return stage_order_.size(); }

int BenchmarkRunner::GetStageIndex(const std::string& stage_name) const {
  auto it = std::find(stage_order_.begin(), stage_order_.end(), stage_name);
  return it == stage_order_.end() ? -1
                                  : std::distance(stage_order_.begin(), it);
}

void BenchmarkRunner::AddCustomStage(const std::string& name,
                                     const CustomStage::RunFunc& stage_function,
                                     const std::string& last_stage) {
  if (auto custom_stage = std::make_shared<CustomStage>(
          logger_, *config_, *unpacker_, *ep_dependencies_manager_, name,
          stage_function))
    AddCustomStage(std::dynamic_pointer_cast<StageBase>(custom_stage),
                   last_stage);
}

void BenchmarkRunner::AddCustomStage(std::shared_ptr<StageBase> stage,
                                     const std::string& last_stage) {
  stages_[stage->GetName()] = stage;
  if (!last_stage.empty()) {
    stage_order_.insert(
        std::find(stage_order_.begin(), stage_order_.end(), last_stage) + 1,
        stage->GetName());
  } else {
    stage_order_.push_back(stage->GetName());
  }
}

}  // namespace cil
