#ifndef EXECUTION_PROVIDER_CONFIG_H_
#define EXECUTION_PROVIDER_CONFIG_H_

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "model_config.h"

#undef max

namespace cil {

class ExecutionProviderConfig {
 public:
  ExecutionProviderConfig() = default;
  inline ExecutionProviderConfig(const nlohmann::json& j) { FromJson(j); }
  ~ExecutionProviderConfig() = default;

  void FromJson(const nlohmann::json& j, const std::string& base_dir = "") {
    from_json(j, *this, base_dir);
  }

  static void from_json(const nlohmann::json& j, ExecutionProviderConfig& obj,
                        const std::string& base_dir = "") {
    j.at("Name").get_to(obj.name_);
    // this is not a required field
    obj.config_ = j.value("Config", nlohmann::json());

    if (j.contains("LibraryPath")) {
      j.at("LibraryPath").get_to(obj.library_path_);
    }
    if (j.contains("Dependencies")) {
      j.at("Dependencies").get_to(obj.dependencies_);
    }
    if (j.contains("Models")) {
      const auto& models_json = j.at("Models");
      for (const auto& model_json : models_json) {
        ModelConfig model;
        model.FromJson(model_json, base_dir);
        obj.models_.emplace_back(model);
      }
    }
  }

  nlohmann::json ToJson() const {
    nlohmann::json j;
    j["Name"] = name_;
    j["Config"] = config_;
    if (!library_path_.empty()) {
      j["LibraryPath"] = library_path_;
    }
    if (!dependencies_.empty()) {
      j["Dependencies"] = dependencies_;
    }
    if (!models_.empty()) {
      j["Models"] = nlohmann::json::array();
      for (const auto& model : models_) {
        j["Models"].push_back(model.ToJson());
      }
    }

    return j;
  }

  const std::string& GetName() const { return name_; }
  const nlohmann::json& GetConfig() const { return config_; }
  const std::string& GetLibraryPath() const { return library_path_; }
  const std::vector<std::string>& GetDependencies() const {
    return dependencies_;
  }
  const std::vector<ModelConfig>& GetModels() const { return models_; }

  inline ExecutionProviderConfig OverrideConfig(
      nlohmann::json new_config) const {
    ExecutionProviderConfig copy;
    copy.name_ = name_;
    copy.config_ = new_config;
    copy.library_path_ = library_path_;
    copy.dependencies_ = dependencies_;
    copy.models_ = models_;
    return copy;
  }

  bool HasModelFileConfig(const std::string& file_path) const {
    for (const auto& model : models_)
      if (model.GetFilePath() == file_path) return true;
    return false;
  }

  bool HasModelConfig(const std::string& model_name) const {
    for (const auto& model : models_)
      if (model.GetName() == model_name) return true;
    return false;
  }

  ModelConfig GetModelByFilePath(const std::string& file_path) const {
    for (const auto& model : models_)
      if (model.GetFilePath() == file_path) return model;
    return ModelConfig();
  }

 private:
  std::string name_;
  nlohmann::json config_;
  std::string library_path_;
  std::vector<std::string> dependencies_;
  std::vector<ModelConfig> models_;
};

}  // namespace cil

#endif  // !EXECUTION_PROVIDER_CONFIG_H_
