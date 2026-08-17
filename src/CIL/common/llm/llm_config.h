#ifndef LLM_CONFIG_H_
#define LLM_CONFIG_H_

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"

namespace cil::infer {

enum class SearchMethod { kGreedy = 0, kTopK = 1, kTopP = 2, kBeamSearch = 3 };

class SearchConfig {
 public:
  SearchMethod method = SearchMethod::kGreedy;
  int top_k = 1;
  double top_p = 0.9;
  double temperature = 0.6;
  bool stop_on_eos = true;
  int max_length = 1024;
  int max_total_length = 0;
};

// In-memory form of a prompt JSON's "model_config" section. The flat layout
// (context_length directly on model_config + search) is canonical; the legacy
// nested form (model_config.model.context_length) is still accepted by
// GetContextLength() for backward compatibility.
class LlmConfig {
 public:
  explicit LlmConfig(Logger logger);

  int batch_size = 1;

  int context_length = 4096;
  SearchConfig search;

  Logger logger_;

  bool LoadFromJson(const nlohmann::json& j);
  bool LoadFromFile(const std::string& config_file);
  bool LoadFromFile(const std::string& config_file,
                    const std::string& schema_file);

  // Reads context_length from a model_config JSON, accepting either the flat
  // form (context_length directly on model_config) or the legacy nested form
  // (model_config.model.context_length). The flat form takes precedence when
  // both are present.
  static int GetContextLength(const nlohmann::json& model_config,
                              int default_value = 4096);
};

}  // namespace cil::infer

#endif  // LLM_CONFIG_H_
