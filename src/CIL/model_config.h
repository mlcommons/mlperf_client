#ifndef MODEL_CONFIG_H_
#define MODEL_CONFIG_H_
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

namespace cil {

class ModelConfig {
 public:
  ModelConfig() = default;
  ModelConfig(const nlohmann::json& j) { FromJson(j); }
  ~ModelConfig() = default;

  const std::string& GetName() const { return name_; }
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
  std::string file_path_;
  std::string data_file_path_;
  std::string tokenizer_path_;
  std::unordered_set<std::string> additional_paths_;
};

}  // namespace cil

#endif  // MODEL_CONFIG_H_
