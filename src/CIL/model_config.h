#ifndef MODEL_CONFIG_H_
#define MODEL_CONFIG_H_

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cil {

class ModelConfig {
 public:
  ModelConfig() = default;
  ModelConfig(const nlohmann::json& j) { FromJson(j); }
  ~ModelConfig() = default;

  const std::string& GetName() const { return name_; }
  const std::string& GetModelBaseName() const { return model_base_name_; }
  void SetModelBaseNameIfEmpty(std::string_view base_name) {
    if (model_base_name_.empty()) model_base_name_ = base_name;
  }
  const std::string& GetModelDisplayName() const { return model_display_name_; }
  void SetModelDisplayNameIfEmpty(std::string_view display_name) {
    if (model_display_name_.empty()) model_display_name_ = display_name;
  }
  // Pretty label for logs / UI. Both candidates are populated by
  // ScenarioConfig::from_json (display name deduced from the model base name
  // when not provided explicitly), so this stays a simple stored-field
  // fallback without depending on BenchmarkRunner here.
  const std::string& GetDisplayName() const {
    return model_display_name_.empty() ? name_ : model_display_name_;
  }
  const std::string& GetFilePath() const { return file_path_; }
  const std::string& GetDataFilePath() const { return data_file_path_; }
  const std::string& GetTokenizerPath() const { return tokenizer_path_; }
  const std::unordered_set<std::string>& GetAdditionalPaths() const {
    return additional_paths_;
  }

  static void from_json(const nlohmann::json& j, ModelConfig& obj,
                        const std::string& base_dir = "");
  void FromJson(const nlohmann::json& j, const std::string& base_dir = "") {
    from_json(j, *this, base_dir);
  }
  nlohmann::json ToJson() const;

 private:
  std::string name_;
  std::string model_base_name_;
  std::string model_display_name_;
  std::string file_path_;
  std::string data_file_path_;
  std::string tokenizer_path_;
  std::unordered_set<std::string> additional_paths_;
};

}  // namespace cil

#endif  // MODEL_CONFIG_H_
