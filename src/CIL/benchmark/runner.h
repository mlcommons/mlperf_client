#ifndef BENCHMARK_RUNNER_H
#define BENCHMARK_RUNNER_H

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    bool collect_file_sizes_only = false;  // in downloading stage collect file
                                           // sizes only (no download or copy)
    std::optional<int> device_id;  // override device id during preparation

    // Needed for preparation stage, must not be empty
    // These paths are used to locate the schemas for various validation during
    // preparation step
    std::string output_results_schema_path;
    std::string data_verification_file_schema_path;
    std::string input_file_schema_path;
    std::string image_input_file_schema_path;

    bool skip_failed_prompts =
        false;  // skip failed prompts during benchmarking
  };

  /**
   * @brief Constructor for BenchmarkRunner class.
   *
   * @param params Parameters for the BenchmarkRunner.
   */
  explicit BenchmarkRunner(Params& params);
  ~BenchmarkRunner() = default;

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

  static void ClearCache(const std::string& deps_dir);

  int GetTotalStages() const;
  int GetStageIndex(const std::string& stage_name) const;

  using PreparedEPList =
      std::vector<std::pair<ExecutionProviderConfig, std::string>>;
  std::vector<PreparedEPList> GetPreparedEPs() const;

  const std::map<std::string, uint64_t>& GetFileSizes() const;

  // Like GetFileSizes() but also carries each file's planned destination path,
  // for callers that need to check disk space per volume.
  const std::map<std::string, FileInfo>& GetFileInfos() const;

  /// First non-empty error message recorded by any stage. Used by callers to
  /// surface a specific reason (e.g. disk-space shortfall) in the GUI status
  /// widget / CLI console instead of a generic "stage failed". Empty when no
  /// stage recorded an error.
  std::string GetLastErrorMessage() const;

  // -------------------------------------------------------------------------
  // Supported scenarios and models
  //
  // BenchmarkRunner is the canonical place for "what does this build of the
  // harness know about?". The types and lookups below are nested here on
  // purpose so the include graph stays small and there is exactly one source
  // of truth for the supported-models table and the scenario alias map.
  // -------------------------------------------------------------------------

  /**
   * @brief Typed handle for each model in the supported-models table.
   *
   * Enumerator names match the canonical ModelBaseName string spelling so a
   * consumer can read `SupportedModel::phi_4_mini_instruct` as the typed
   * equivalent of the string `"phi_4_mini_instruct"`. Use
   * ParseSupportedModel() to go from a string (e.g. config input) to this
   * enum, then look the metadata up in GetSupportedModelsInfo().
   */
  enum class SupportedModel {
    llama_3_1_8b_instruct = 0,
    phi_4_mini_instruct,
    phi_4_reasoning_14b,
    qwen_3_8b,
    flux_2_klein_4b,
  };

  /**
   * @brief Metadata for a supported ModelBaseName.
   *
   * Values of this struct are stored in the supported-models map keyed by
   * SupportedModel; the @ref base_name field is the stringified form of that
   * key, kept here so consumers that already iterate the table can display
   * it without a separate enum-to-string lookup.
   */
  struct ModelInfo {
    enum class Type { kLLM, kImage };

    enum class Category { kBase, kExtended, kExperimental, kCustom };

    std::string base_name;    ///< Canonical ModelBaseName.
    std::string pretty_name;  ///< Human-readable display name.
    Type type;                ///< Modality / workload kind.
    Category category;        ///< Support tier.
  };

  /**
   * @brief The supported-models table.
   *
   * Keyed by @ref SupportedModel so duplicates are impossible at the type
   * level; @c std::map ensures iteration follows the enum order declared in
   * @ref SupportedModel.
   *
   * @return Reference valid for the program's lifetime.
   */
  static const std::map<SupportedModel, ModelInfo>& GetSupportedModelsInfo();

  /**
   * @brief Resolve a ModelBaseName string to its typed @ref SupportedModel
   * enumerator.
   *
   * @return The matching enumerator, or @c std::nullopt when the name is not
   *         in the supported-models table.
   */
  static std::optional<SupportedModel> ParseSupportedModel(
      const std::string_view& model_base_name);

  /**
   * @brief Classify a ModelBaseName against the supported-models table.
   *
   * @return The model's category, or kCustom when the name is not supported.
   */
  static ModelInfo::Category ClassifyModelBaseName(
      const std::string& model_base_name);

  /**
   * @brief Whether @p model_base_name is supported.
   *
   * @return true if supported, false for empty input or unsupported names.
   */
  static bool IsSupportedModel(const std::string& model_base_name);

  /**
   * @brief Human-readable display name for a ModelBaseName.
   *
   * Supported models map to a hardcoded pretty name; unsupported ones are
   * derived via title-case conversion (underscores become spaces).
   */
  static std::string ModelBaseNameToPrettyName(
      const std::string& model_base_name);

  /**
   * @brief Pretty display name for a scenario alias or a ModelBaseName.
   *
   * Shortcut that resolves a scenario alias (e.g. "llama2") to its
   * ModelBaseName and then returns the pretty name; for inputs that already
   * are a ModelBaseName, calls ModelBaseNameToPrettyName directly. Always
   * returns a usable string (empty input maps to empty output).
   *
   * @param name Scenario alias or ModelBaseName as written in the config or
   *        in a previously logged result file.
   */
  static std::string GetModelFullName(const std::string& name);

  /**
   * @brief Whether @p name identifies an LLM scenario.
   *
   * Accepts the canonical "txt2txt" or any supported scenario alias.
   *
   * @param name Scenario name as written in the config.
   * @return true for canonical or aliased LLM scenarios.
   */
  static bool IsLLMScenario(const std::string& name);

  /**
   * @brief Whether @p name identifies an image-generation scenario.
   *
   * Accepts the canonical "txt2img" or any supported image alias.
   *
   * @param name Scenario name as written in the config.
   * @return true for canonical or aliased image scenarios.
   */
  static bool IsImageScenario(const std::string& name);

  /**
   * @brief Resolve a scenario alias to its canonical ModelBaseName.
   *
   * @param scenario_name Scenario name as written in the config
   * (case-insensitive).
   * @return Canonical ModelBaseName implied by the alias, or an empty string
   *         when @p scenario_name is not supported or already canonical.
   */
  static std::string LookupScenarioNameAlias(const std::string& scenario_name);

  /**
   * @brief Canonical scenario names recognised by the harness.
   *
   * @return e.g. {"txt2txt"}.
   */
  static std::vector<std::string> GetScenarioNames();

  /**
   * @brief Scenario alias names accepted at config parse time.
   *
   * Aliases are canonicalised before reaching the rest of the pipeline, see
   * LookupScenarioNameAlias().
   *
   * @return Sorted list of accepted alias strings.
   */
  static std::vector<std::string> GetScenarioNameAliases();

  /**
   * @brief Whether the named EP is supported by this build on the current
   * platform.
   *
   * The check combines two things:
   *   - whether the harness was compiled with that EP (the @c WITH_IHV_*
   *     CMake flag matrix);
   *   - for vendor-bound GGML back-ends, whether a matching GPU is present
   *     on this machine.
   *
   * @param scenario_name Scenario the EP would run (e.g. @c "txt2txt" or a
   * legacy alias). Pass an empty string to skip the scenario filter and only
   * check whether the EP is built into the harness.
   * @param ep_name EP identifier as it appears in the config (with or without
   * the @c "IHV " prefix). May be a qualified name (e.g. "llama-cpp-CUDA"); the
   * backend suffix is resolved internally.
   */
  static bool IsEpSupportedOnThisPlatform(const std::string_view& scenario_name,
                                          const std::string_view& ep_name);

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

  std::vector<PreparedEPList> prepared_eps_;
  bool enumerate_only_;

  bool collect_file_sizes_only_;
  std::map<std::string, uint64_t> file_sizes_;
  std::map<std::string, FileInfo> file_infos_;
};

}  // namespace cil

#endif