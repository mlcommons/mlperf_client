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
  int pad_token_id = 0;
  int vocab_size = 32000;
  int head_size = 128;
  int hidden_size = 4096;
  std::unordered_map<std::string, std::string> input_names{
      {"input_ids", "input_ids"},
      {"attention_mask", "attention_mask"},
      {"position_ids", "position_ids"},
      {"past_key_names", "past_key_values.%d.key"},
      {"past_value_names", "past_key_values.%d.value"}};
  std::unordered_map<std::string, std::string> output_names{
      {"logits", "logits"},
      {"present_key_names", "present.%d.key"},
      {"present_value_names", "present.%d.value"}};
  int num_attention_heads = 32;
  int num_hidden_layers = 32;
  int num_key_value_heads = 32;
};

enum class SearchMethod { kGreedy = 0, kTopK = 1, kTopP = 2, kBeamSearch = 3 };

class SearchConfig {
 public:
  SearchMethod method = SearchMethod::kGreedy;
  int top_k = 1;
  double top_p = 0.9;
  int num_beams = 1;
  double temperature = 0.6;
  bool stop_on_eos = true;
  int max_length = 1024;        // Max generation length
  int max_total_length = 2048;  // Max sequence length, including the prompt
  float length_penalty = 0.75;
  int seed = 0;
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
