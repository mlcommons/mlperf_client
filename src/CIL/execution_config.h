#ifndef EXECUTION_SCENARIO_CONFIG_H_
#define EXECUTION_SCENARIO_CONFIG_H_

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "execution_provider_config.h"

namespace cil {

class SystemConfig {
 public:
  SystemConfig() = default;
  SystemConfig(const nlohmann::json& j) { FromJson(j); }
  ~SystemConfig() = default;

  const std::string& GetComment() const { return comment_; }
  const std::string& GetTempPath() const { return temp_path_; }
  const std::string& GetEPDependenciesConfigPath() const {
    return ep_dependencies_config_path_;
  }
  const std::string& GetBaseDir() const { return base_dir_; }

  bool IsTempPathCorrect() const { return is_temp_path_correct_; }
  bool IsBaseDirCorrect() const { return is_base_dir_correct_; }

  static void from_json(const nlohmann::json& j, SystemConfig& obj);
  void FromJson(const nlohmann::json& j) { from_json(j, *this); }
  nlohmann::json ToJson() const {
    nlohmann::json j;
    j["Comment"] = comment_;
    j["TempPath"] = temp_path_;
    j["EPDependenciesConfigPath"] = ep_dependencies_config_path_;
    return j;
  }
  const std::string& GetDownloadBehavior() const { return download_behavior_; }
  void SetDownloadBehavior(const std::string& download_behavior) {
    download_behavior_ = download_behavior;
  }
  bool GetCacheLocalFiles() const { return cache_local_files_; }
  void SetCacheLocalFiles(bool cache_local_files) {
    cache_local_files_ = cache_local_files;
  }

 private:
  std::string comment_;
  std::string temp_path_;
  bool is_temp_path_correct_ = false;
  std::string ep_dependencies_config_path_;
  bool is_base_dir_correct_ = false;
  std::string base_dir_;
  std::string download_behavior_ = "normal";
  bool cache_local_files_ = true;
};

class ScenarioConfig {
 public:
  ScenarioConfig()
      : iterations_(0), iterations_warmup_(0), inference_delay_(0.0) {};
  ScenarioConfig(const nlohmann::json& j) { FromJson(j); }
  ~ScenarioConfig() = default;

  const std::string& GetName() const { return name_; }
  const std::vector<ModelConfig>& GetModels() const { return models_; }
  const std::vector<std::string>& GetInputs() const { return inputs_; }
  const std::vector<std::string>& GetAssets() const { return assets_; }
  const std::string& GetResultsFile() const { return results_file_; }
  const std::string& GetDataVerificationFile() const {
    return data_verification_file_;
  }
  int GetIterations() const { return iterations_; }
  int GetIterationsWarmUp() const { return iterations_warmup_; }
  double GetInferenceDelay() const { return inference_delay_; }
  const std::vector<ExecutionProviderConfig>& GetExecutionProviders() const {
    return execution_providers_;
  }
  void SetExecutionProviderConfig(int ep_index, const nlohmann::json& config) {
    execution_providers_.at(ep_index).SetConfig(config);
  }

  /**
   * @brief Retrieves the global and overridden model files along with their
   * unique keys.
   *
   * This function generates a unique key for each global and overridden model
   * file path. These keys can be used to identify and match corresponding extra
   * files associated with the model. The function returns a map
   * where each entry consists of a file path as the key and its corresponding
   * unique key as the value.
   *
   * @return A map containing model file paths as keys and their corresponding
   * unique keys as values.
   */
  using ModelsMap = std::vector<std::pair<std::string, std::string>>;

  ModelsMap GetModelFiles() const;
  std::unordered_map<std::string, std::string> GetModelExtraFiles() const;
  ModelConfig GetModelByFilePath(const std::string& file_path) const;

  const std::map<std::string, std::vector<std::string>>
  GetExecutionProvidersDependencies() const;

  static void from_json(const nlohmann::json& j, ScenarioConfig& obj,
                        const std::string& base_dir);
  void FromJson(const nlohmann::json& j, const std::string& base_dir = "") {
    from_json(j, *this, base_dir);
  }
  nlohmann::json ToJson() const;
  // Interface for the config creator
  void AddModel(const ModelConfig& model) { models_.emplace_back(model); }
  void RemoveModel(const std::string& model_name) {
    models_.erase(std::remove_if(models_.begin(), models_.end(),
                                 [&model_name](const ModelConfig& model) {
                                   return model.GetName() == model_name;
                                 }),
                  models_.end());
  }
  void AddInput(const std::string& input) { inputs_.emplace_back(input); }
  void RemoveInput(const std::string& input) {
    inputs_.erase(std::remove(inputs_.begin(), inputs_.end(), input),
                  inputs_.end());
  }
  void AddAsset(const std::string& asset) { assets_.emplace_back(asset); }
  void RemoveAsset(const std::string& asset) {
    assets_.erase(std::remove(assets_.begin(), assets_.end(), asset),
                  assets_.end());
  }
  void SetResultsFile(const std::string& results_file) {
    results_file_ = results_file;
  }
  void SetDataVerificationFile(const std::string& data_verification_file) {
    data_verification_file_ = data_verification_file;
  }
  void SetIterations(int iterations) { iterations_ = iterations; }

  void AddExecutionProvider(const ExecutionProviderConfig& ep) {
    execution_providers_.emplace_back(ep);
  }

  void RemoveExecutionProvider(const std::string& ep_name) {
    execution_providers_.erase(
        std::remove_if(execution_providers_.begin(), execution_providers_.end(),
                       [&ep_name](const ExecutionProviderConfig& ep) {
                         return ep.GetName() == ep_name;
                       }),
        execution_providers_.end());
  }

  void SetName(const std::string& name) { name_ = name; }

 private:
  std::string name_;
  std::vector<ModelConfig> models_;
  std::vector<std::string> inputs_;
  std::vector<std::string> assets_;
  std::string results_file_;
  std::string data_verification_file_;
  int iterations_;
  int iterations_warmup_;
  double inference_delay_;
  std::vector<ExecutionProviderConfig> execution_providers_;
};

class ExecutionConfig {
 public:
  ExecutionConfig() = default;
  ~ExecutionConfig() = default;

  bool ValidateAndParse(
      const std::string& json_file_path, const std::string& schema_file_path,
      const std::string& config_verification_file_path,
      const std::string& config_experimental_verification_file_path = {});

  bool IsConfigVerified() const { return config_verified_; }
  void SetConfigVerified(bool verified) { config_verified_ = verified; }

  bool IsConfigExperimental() const { return config_experimental_; }

  std::string GetConfigFileName() const { return config_file_name_; }

  std::string GetConfigFileHash() const { return config_file_hash_; }

  const std::vector<ScenarioConfig>& GetScenarios() const { return scenarios_; }
  std::vector<ScenarioConfig>& GetScenarios() { return scenarios_; }
  const SystemConfig& GetSystemConfig() const { return system_config_; }
  SystemConfig& GetSystemConfig() { return system_config_; }

  static void from_json(const nlohmann::json& j, ExecutionConfig& obj);
  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  static bool isConfigFileValid(const std::string& json_path);
  static nlohmann::json GetEPsConfigSchema(const std::string& schema_file_path);

 private:
  bool VerifyConfigFileHash(std::string_view verification_file_path,
                            std::string_view hash,
                            std::string& file_name) const;

  std::vector<ScenarioConfig> scenarios_;
  SystemConfig system_config_;

  std::string config_file_name_;
  std::string config_file_hash_;

  bool config_verified_{false};
  bool config_experimental_{false};
};

std::filesystem::path GetExecutionProviderParentLocation(
    const cil::ExecutionProviderConfig& ep_config, const std::string& deps_dir);

}  // namespace cil

#endif  // EXECUTION_SCENARIO_CONFIG_H_
