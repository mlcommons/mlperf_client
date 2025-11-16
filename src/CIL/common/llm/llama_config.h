#ifndef LLAMA_CONFIG_H_
#define LLAMA_CONFIG_H_

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"

namespace cil {
namespace infer {

class ModelConfig {
 public:
  int bos_token_id = 1;
  int context_length = 4096;
  int eos_token_id = 2;
  int vocab_size = 32000;
};

enum class SearchMethod { kGreedy = 0, kTopK = 1, kTopP = 2, kBeamSearch = 3 };

class SearchConfig {
 public:
  SearchMethod method = SearchMethod::kGreedy;
  int top_k = 1;
  double top_p = 0.9;
  double temperature = 0.6;
  bool stop_on_eos = true;
  int max_length = 1024;        // Max generation length
  int max_total_length = 2048;  // Max sequence length, including the prompt
};

class LlamaConfig {
 public:
  LlamaConfig(Logger logger);

  int batch_size = 1;

  ModelConfig model;
  SearchConfig search;

  Logger logger_;

  bool LoadFromJson(const nlohmann::json& j);
  bool LoadFromFile(const std::string& config_file);
  bool LoadFromFile(const std::string& config_file,
                    const std::string& schema_file);
};

}  // namespace infer
}  // namespace cil

#endif  // !LLAMA_CONFIG_H_
