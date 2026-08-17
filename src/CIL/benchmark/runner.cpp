#include "runner.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "benchmark_stage.h"
#include "data_verification_stage.h"
#include "download_stage.h"
#include "execution_provider.h"
#include "preparation_stage.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {

namespace {

using SupportedModel = BenchmarkRunner::SupportedModel;
using ModelInfo = BenchmarkRunner::ModelInfo;

/// Accepted scenario aliases and the ModelBaseName each one resolves to.
const std::unordered_map<std::string, std::string>
    kScenarioNameAliasToModelBaseName = {
        {"llama3", "llama_3_1_8b_instruct"},
        {"phi4mini", "phi_4_mini_instruct"},
        {"phi4reason", "phi_4_reasoning_14b"},
        {"qwen3", "qwen_3_8b"},
        // Deprecated: pre-mini "phi4" alias kept for backward compatibility
        // with existing configs; new configs should use "phi4reason".
        {"phi4", "phi_4_reasoning_14b"},
};

/// Supported models known to this build of the harness, keyed by the typed
/// SupportedModel handle so duplicates are impossible at the type level.
const std::map<SupportedModel, ModelInfo> kSupportedModelsInfo = {
    {SupportedModel::llama_3_1_8b_instruct,
     {"llama_3_1_8b_instruct", "Llama 3.1 8B Instruct", ModelInfo::Type::kLLM,
      ModelInfo::Category::kBase}},
    {SupportedModel::phi_4_mini_instruct,
     {"phi_4_mini_instruct", "Phi 4 Mini Instruct", ModelInfo::Type::kLLM,
      ModelInfo::Category::kBase}},
    {SupportedModel::phi_4_reasoning_14b,
     {"phi_4_reasoning_14b", "Phi 4 Reasoning 14B", ModelInfo::Type::kLLM,
      ModelInfo::Category::kExtended}},
    {SupportedModel::qwen_3_8b,
     {"qwen_3_8b", "Qwen 3 8B", ModelInfo::Type::kLLM,
      ModelInfo::Category::kBase}},
    {SupportedModel::flux_2_klein_4b,
     {"flux_2_klein_4b", "Flux 2 Klein 4B", ModelInfo::Type::kImage,
      ModelInfo::Category::kExperimental}},
};

/// Accepted image scenario aliases and the ModelBaseName each one resolves to.
const std::unordered_map<std::string, std::string>
    kImageScenarioNameAliasToModelBaseName = {
        {"flux2klein", "flux_2_klein_4b"},
};

const std::unordered_set<std::string>& GetImageScenarioNames() {
  static const std::unordered_set<std::string> names = [] {
    std::unordered_set<std::string> s = {"txt2img"};
    for (const auto& [alias, _] : kImageScenarioNameAliasToModelBaseName) {
      s.emplace(alias);
    }
    return s;
  }();
  return names;
}

const std::unordered_set<std::string>& GetLLMScenarioNames() {
  static const std::unordered_set<std::string> names = [] {
    std::unordered_set<std::string> s = {"txt2txt"};
    for (const auto& [alias, _] : kScenarioNameAliasToModelBaseName) {
      s.emplace(alias);
    }
    return s;
  }();
  return names;
}

std::optional<ModelInfo> FindSupportedModel(const std::string_view& base_name) {
  for (const auto& [_, info] : kSupportedModelsInfo) {
    if (info.base_name == base_name) return info;
  }
  return std::nullopt;
}

}  // namespace

BenchmarkRunner::BenchmarkRunner(Params& params)
    : progress_handler_(params.progress_handler),
      config_(params.config),
      unpacker_(params.unpacker),
      ep_dependencies_manager_(params.ep_dependencies_manager),
      results_logger_(std::make_unique<BenchmarkLogger>(params.output_dir)),
      logger_(params.logger),
      enumerate_only_(params.enumerate_only),
      collect_file_sizes_only_(params.collect_file_sizes_only) {
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

  if (params.collect_file_sizes_only) {
    // Combined with enumeration, size only the EP dependencies that the
    // enumeration download actually fetches (not the full model assets).
    config_->GetSystemConfig().SetDownloadBehavior(
        params.enumerate_only ? "collect_file_sizes_deps_only"
                              : "collect_file_sizes_only");
  } else if (params.enumerate_only) {
    config_->GetSystemConfig().SetDownloadBehavior("deps_only_enumeration");
  }

  auto download_stage = std::make_shared<DownloadStage>(
      logger_, *config_, *unpacker_, *ep_dependencies_manager_,
      params.data_dir);
  stages_[download_stage->GetName()] = download_stage;
  stage_order_.emplace_back("Download");

  auto preparation_stage = std::make_shared<PreparationStage>(
      logger_, *config_, *unpacker_, *ep_dependencies_manager_,
      params.enumerate_only, params.device_id);
  stages_[preparation_stage->GetName()] = preparation_stage;
  stage_order_.emplace_back("Preparation");

  if (!params.enumerate_only) {
    auto data_verification_stage = std::make_shared<DataVerificationStage>(
        logger_, *config_, *unpacker_, *ep_dependencies_manager_,
        params.data_verification_file_schema_path);
    stages_[data_verification_stage->GetName()] = data_verification_stage;
    stage_order_.emplace_back("DataVerification");

    auto benchmark_stage = std::make_shared<BenchmargStage>(
        logger_, *config_, *unpacker_, *ep_dependencies_manager_,
        *results_logger_, params.output_results_schema_path,
        params.input_file_schema_path, params.image_input_file_schema_path,
        params.skip_failed_prompts);
    stages_[benchmark_stage->GetName()] = benchmark_stage;
    stage_order_.emplace_back("Benchmark");
  }

  on_enter_stage_ = [](const std::string_view&, int) { return true; };
  on_failed_stage_ = [](const std::string_view&, int) { return false; };
}

void BenchmarkRunner::ClearCache(const std::string& deps_dir) {
  DownloadStage::ClearCache(deps_dir);
}

std::vector<BenchmarkRunner::PreparedEPList> BenchmarkRunner::GetPreparedEPs()
    const {
  return prepared_eps_;
}

const std::map<std::string, uint64_t>& BenchmarkRunner::GetFileSizes() const {
  return file_sizes_;
}

const std::map<std::string, FileInfo>& BenchmarkRunner::GetFileInfos() const {
  return file_infos_;
}

std::string BenchmarkRunner::GetLastErrorMessage() const {
  for (const auto& name : stage_order_) {
    auto it = stages_.find(name);
    if (it == stages_.end()) continue;
    const auto& msg = it->second->GetErrorMessage();
    if (!msg.empty()) return msg;
  }
  return {};
}

const std::map<BenchmarkRunner::SupportedModel, BenchmarkRunner::ModelInfo>&
BenchmarkRunner::GetSupportedModelsInfo() {
  return kSupportedModelsInfo;
}

std::optional<BenchmarkRunner::SupportedModel>
BenchmarkRunner::ParseSupportedModel(const std::string_view& model_base_name) {
  for (const auto& [model, info] : kSupportedModelsInfo) {
    if (info.base_name == model_base_name) return model;
  }
  return std::nullopt;
}

BenchmarkRunner::ModelInfo::Category BenchmarkRunner::ClassifyModelBaseName(
    const std::string& model_base_name) {
  if (auto m = FindSupportedModel(model_base_name)) return m->category;
  return ModelInfo::Category::kCustom;
}

bool BenchmarkRunner::IsSupportedModel(const std::string& model_base_name) {
  return !model_base_name.empty() &&
         ClassifyModelBaseName(model_base_name) != ModelInfo::Category::kCustom;
}

std::string BenchmarkRunner::ModelBaseNameToPrettyName(
    const std::string& model_base_name) {
  if (auto m = FindSupportedModel(model_base_name)) return m->pretty_name;
  return utils::StringToTitleCase(model_base_name);
}

std::string BenchmarkRunner::GetModelFullName(const std::string& name) {
  if (auto aliased = LookupScenarioNameAlias(name); !aliased.empty())
    return ModelBaseNameToPrettyName(aliased);
  return ModelBaseNameToPrettyName(name);
}

bool BenchmarkRunner::IsLLMScenario(const std::string& name) {
  return GetLLMScenarioNames().contains(utils::StringToLowerCase(name));
}

bool BenchmarkRunner::IsImageScenario(const std::string& name) {
  return GetImageScenarioNames().contains(utils::StringToLowerCase(name));
}

std::string BenchmarkRunner::LookupScenarioNameAlias(
    const std::string& scenario_name) {
  const std::string lower = utils::StringToLowerCase(scenario_name);
  if (const auto it = kScenarioNameAliasToModelBaseName.find(lower);
      it != kScenarioNameAliasToModelBaseName.end())
    return it->second;
  if (const auto it = kImageScenarioNameAliasToModelBaseName.find(lower);
      it != kImageScenarioNameAliasToModelBaseName.end())
    return it->second;
  return std::string{};
}

std::vector<std::string> BenchmarkRunner::GetScenarioNames() {
  return {"txt2txt", "txt2img"};
}

std::vector<std::string> BenchmarkRunner::GetScenarioNameAliases() {
  std::vector<std::string> result;
  result.reserve(kScenarioNameAliasToModelBaseName.size() +
                 kImageScenarioNameAliasToModelBaseName.size());
  for (const auto& [alias, _] : kScenarioNameAliasToModelBaseName) {
    result.emplace_back(alias);
  }
  for (const auto& [alias, _] : kImageScenarioNameAliasToModelBaseName) {
    result.emplace_back(alias);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool BenchmarkRunner::IsEpSupportedOnThisPlatform(
    const std::string_view& scenario_name, const std::string_view& ep_name) {
  if (const std::string name_str(scenario_name); !scenario_name.empty() &&
                                                 !IsLLMScenario(name_str) &&
                                                 !IsImageScenario(name_str))
    return false;

  // Split a qualified name ("llama-cpp-CUDA"); names NameToEP already knows
  // (e.g. "OrtGenAI-RyzenAI") are taken whole. The backend is matched
  // case-insensitively, so "llama-cpp-cuda" works too.
  std::string base(ep_name);
  std::string backend;
  if (NameToEP(base) == EP::kUnknown)
    if (const auto pos = base.rfind('-'); pos != std::string::npos) {
      backend = utils::StringToLowerCase(base.substr(pos + 1));
      base.resize(pos);
    }

  const std::string base_lower = utils::StringToLowerCase(base);

  // Match base through NameToEP so its known spellings resolve in one place,
  // then fall back to a case-insensitive compare of the plain and
  // "IHV "-prefixed name (covers spellings NameToEP doesn't enumerate).
  auto is_supported_ihv = [&](const std::string& name) {
    const std::string name_lower = utils::StringToLowerCase(name);
    return NameToEP(base) == NameToEP(name) || base_lower == name_lower ||
           base_lower == "ihv " + name_lower;
  };

#if WITH_IHV_NATIVE_OPENVINO
  if (is_supported_ihv("NativeOpenVINO")) return true;
#endif
#if WITH_IHV_NATIVE_QNN
  if (is_supported_ihv("NativeQNN")) return true;
#endif
#if WITH_IHV_ORT_GENAI
  if (is_supported_ihv("OrtGenAI")) return true;
#endif
#if WITH_IHV_ORT_GENAI_RYZENAI
  if (is_supported_ihv("OrtGenAI-RyzenAI"))
    return utils::SupportsVendorID(utils::VendorID::kAMD);
#endif
#if WITH_IHV_WIN_ML
  if (is_supported_ihv("WindowsML")) return true;
#endif
#if WITH_IHV_GGML_METAL || WITH_IHV_GGML_VULKAN || WITH_IHV_GGML_CUDA || \
    WITH_IHV_GGML_ROCM
  if (is_supported_ihv("llama-cpp")) {
#if WITH_IHV_GGML_METAL
    if (backend == "metal") return true;
#endif
#if WITH_IHV_GGML_VULKAN
    if (backend == "vulkan") return true;
#endif
#if WITH_IHV_GGML_CUDA
    if (backend == "cuda")
      return utils::SupportsVendorID(utils::VendorID::kNVIDIA);
#endif
#if WITH_IHV_GGML_ROCM
    if (backend == "rocm")
      return utils::SupportsVendorID(utils::VendorID::kAMD);
#endif
    return backend.empty();
  }
#endif
#if WITH_IHV_MLX
  if (is_supported_ihv("MLX")) return true;
#endif
#if WITH_IHV_DIFFUSERS
  if (is_supported_ihv("Diffusers")) {
#if WITH_IHV_DIFFUSERS_NVIDIA
    if (backend == "cuda")
      return utils::SupportsVendorID(utils::VendorID::kNVIDIA);
#endif
#if WITH_IHV_DIFFUSERS_AMD
    if (backend == "ryzenai")
      return utils::SupportsVendorID(utils::VendorID::kAMD);
#endif
#if WITH_IHV_DIFFUSERS_APPLE
    if (backend == "mps") return true;
#endif
    return backend.empty();
  }
#endif

  return false;
}

bool BenchmarkRunner::ReportProgress(ProgressTracker& progress_tracker,
                                     TaskScheduler& task_scheduler) {
  if (progress_handler_)
    progress_handler_->StartTracking(progress_tracker);
  else
    progress_tracker.StartTracking();

  task_scheduler.ExecuteTasksAsync();

  // Treat update_interval as the UI refresh upper bound, not a hard sleep:
  // wait on the scheduler's completion CV so the loop exits immediately when
  // all tasks finish and still ticks at update_interval for active downloads.
  const auto wait_ms =
      static_cast<unsigned int>(progress_tracker.GetUpdateInterval().count());

  if (progress_handler_) {
    auto& interrupt = progress_handler_->GetInterrupt();

    while (!progress_tracker.Finished()) {
      progress_handler_->Update(progress_tracker);
      if (progress_tracker.Finished()) break;
      task_scheduler.WaitForCompletionWithTimeout(wait_ms);
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
      task_scheduler.WaitForCompletionWithTimeout(wait_ms);
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
    if (collect_file_sizes_only_) {
      for (const auto& [url, info] : scenario_data.file_sizes) {
        file_sizes_[url] = info.size;
        file_infos_[url] = info;
      }
    }
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
