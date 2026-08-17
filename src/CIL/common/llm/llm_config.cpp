#include "llm_config.h"

#include "../json_schema.h"

namespace cil::infer {

int LlmConfig::GetContextLength(const nlohmann::json& model_config,
                                     int default_value) {
  if (model_config.contains("context_length")) {
    return model_config.value("context_length", default_value);
  }
  if (model_config.contains("model") && model_config["model"].is_object() &&
      model_config["model"].contains("context_length")) {
    return model_config["model"].value("context_length", default_value);
  }
  return default_value;
}

LlmConfig::LlmConfig(Logger logger) : logger_(logger) {}

bool LlmConfig::LoadFromJson(const nlohmann::json& j) {
  context_length = GetContextLength(j, context_length);

  auto search_config = j["search"];
  auto search_method = search_config["method"];
  if (search_method == "greedy") {
    search.method = SearchMethod::kGreedy;
  } else if (search_method == "top_k") {
    search.method = SearchMethod::kTopK;
    search.top_k = search_config["top_k"];
  } else if (search_method == "top_p") {
    search.method = SearchMethod::kTopP;
    search.top_p = search_config["top_p"];
  } else {
    return false;
  }
  search.temperature = search_config["temperature"];
  search.stop_on_eos = search_config["stop_on_eos"];
  search.max_length = search_config["max_length"];
  search.max_total_length =
      search_config.value("max_total_length", context_length);

  if (search.max_total_length <= search.max_length) {
    logger_(LogLevel::kError,
            "max_total_length must be bigger than max_length.");
    return false;
  }

  return true;
}

bool LlmConfig::LoadFromFile(const std::string& config_file) {
  if (!std::filesystem::exists(config_file)) {
    logger_(LogLevel::kError, "Config file not found: " + config_file);
    return false;
  }

  nlohmann::json j;
  std::ifstream i(config_file);
  i >> j;

  return LoadFromJson(j);
}

bool LlmConfig::LoadFromFile(const std::string& config_file,
                                  const std::string& schema_file) {
  if (!std::filesystem::exists(config_file)) {
    logger_(LogLevel::kError, "Config file not found: " + config_file);
    return false;
  }
  if (!std::filesystem::exists(schema_file)) {
    logger_(LogLevel::kError, "Schema file not found: " + config_file);
    return false;
  }
  std::string error_string = cil::ValidateJSONSchema(schema_file, config_file);
  if (!error_string.empty()) {
    logger_(LogLevel::kError, "Config file validation failed: " + config_file +
                                  ", " + error_string);
    return false;
  }
  return LoadFromFile(config_file);
}

}  // namespace cil::infer
