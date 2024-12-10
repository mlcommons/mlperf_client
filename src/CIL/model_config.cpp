#include "model_config.h"

#include <nlohmann/json.hpp>

#include "utils.h"

namespace cil {

void ModelConfig::from_json(const nlohmann::json& j, ModelConfig& obj,
                            const std::string& base_dir) {
  j.at("ModelName").get_to(obj.name_);
  j.at("FilePath").get_to(obj.file_path_);

  obj.file_path_ = utils::PrependBaseDirIfNeeded(obj.file_path_, base_dir);

  // Handle additional properties including TokenizerPath and DataFilePath
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& key = it.key();
    if (key == "TokenizerPath") {
      j.at("TokenizerPath").get_to(obj.tokenizer_path_);
      obj.tokenizer_path_ =
          utils::PrependBaseDirIfNeeded(obj.tokenizer_path_, base_dir);
    } else if (key == "DataFilePath") {
      j.at("DataFilePath").get_to(obj.data_file_path_);
      obj.data_file_path_ =
          utils::PrependBaseDirIfNeeded(obj.data_file_path_, base_dir);
    } else if (key != "ModelName" && key != "FilePath") {
      std::string path = it.value().get<std::string>();
      path = utils::PrependBaseDirIfNeeded(path, base_dir);
      obj.additional_paths_.insert(path);
    }
  }
}
nlohmann::json ModelConfig::ToJson() const {
  nlohmann::json j;
  j["ModelName"] = name_;
  j["FilePath"] = file_path_;
  if (!data_file_path_.empty()) j["DataFilePath"] = data_file_path_;
  if (!tokenizer_path_.empty()) j["TokenizerPath"] = tokenizer_path_;
  return j;
}

}  // namespace cil
