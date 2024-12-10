#include "llama_config.h"

#include "../json_schema.h"

namespace cil {
namespace infer {

LlamaConfig::LlamaConfig(Logger logger) : logger_(logger) {}

bool LlamaConfig::LoadFromJson(const nlohmann::json& j) {
  // Load model config
  auto model_config = j["model"];
  model.bos_token_id = model_config["bos_token_id"];
  model.context_length = model_config["context_length"];
  model.eos_token_id = model_config["eos_token_id"];
  model.pad_token_id = model_config["pad_token_id"];
  model.vocab_size = model_config["vocab_size"];
  auto decoder_config = model_config["decoder"];
  model.head_size = decoder_config["head_size"];
  model.hidden_size = decoder_config["hidden_size"];
  model.num_attention_heads = decoder_config["num_attention_heads"];
  model.num_hidden_layers = decoder_config["num_hidden_layers"];
  model.num_key_value_heads = decoder_config["num_key_value_heads"];
  auto input_names = decoder_config["inputs"];
  for (auto& [key, value] : input_names.items()) {
    model.input_names[key] = value;
  }
  auto output_names = decoder_config["outputs"];
  for (auto& [key, value] : output_names.items()) {
    model.output_names[key] = value;
  }

  // Load search config
  auto search_config = j["search"];
  auto search_method = search_config["method"];
  if (search_method == "greedy") {
    search.method = SearchMethod::kGreedy;
    search.num_beams = 1;
  } else if (search_method == "top_k") {
    search.method = SearchMethod::kTopK;
    search.top_k = search_config["top_k"];
    search.num_beams = 1;
  } else if (search_method == "top_p") {
    search.method = SearchMethod::kTopP;
    search.top_p = search_config["top_p"];
    search.num_beams = 1;
  } else if (search_method == "beam_search") {
    search.method = SearchMethod::kBeamSearch;
    search.top_k = search_config["top_k"];
    search.top_p = search_config["top_p"];
    search.num_beams = search_config["num_beams"];
  } else {
    return false;
  }
  search.temperature = search_config["temperature"];
  search.stop_on_eos = search_config["stop_on_eos"];
  search.max_length = search_config["max_length"];
  search.max_total_length = search_config["max_total_length"];
  if (search.max_total_length <= search.max_length) {
    logger_(LogLevel::kError,
            "max_total_length (prompt size + generation size) must be "
            "bigger than max_length (generation size).");
    return false;
  }

  return true;
}

bool LlamaConfig::LoadFromFile(const std::string& config_file) {
  if (!std::filesystem::exists(config_file)) {
    logger_(LogLevel::kError, "Config file not found: " + config_file);
    return false;
  }

  nlohmann::json j;
  std::ifstream i(config_file);
  i >> j;

  return LoadFromJson(j);
}

bool LlamaConfig::LoadFromFile(const std::string& config_file,
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

}  // namespace infer
}  // namespace cil
