#ifndef BENCHMARK_RESULT_H_
#define BENCHMARK_RESULT_H_

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cil {

struct PerformanceResult {
  double time_to_first_token_duration = 0.;   // Geomean TTFT (sec)
  double token_generation_rate = 0.0;         // 2+ token/second rate
  uint64_t average_input_tokens = 0;          // Average input tokens provided to LLM pipeline
  uint64_t average_generated_tokens = 0;      // Average tokens generated for all runs
};

using CategoryPerformanceResult = std::unordered_map<std::string, PerformanceResult>;

struct BenchmarkResult {
  std::string config_file_name;
  std::string config_file_hash;
  std::string scenario_name;
  std::string model_path;
  std::vector<std::string> asset_paths;
  std::vector<std::string> data_paths;
  std::string results_file;
  std::string model_name;
  std::string model_file_name;
  std::vector<std::string> asset_file_names;
  std::vector<std::string> data_file_names;
  std::string execution_provider_name;
  nlohmann::json ep_configuration;
  int iterations = 0;
  int iterations_warmup = 0;
  bool benchmark_success = false;
  std::string benchmark_start_time;
  double duration = 0.0;                      // Average time per iteration
  bool results_verified = false;
  std::string benchmark_duration;
  std::string error_message;
  std::string device_type;

  // LLM stuffs
  bool is_llm_benchmark = false;
  double total_inference_duration = 0.0;      // Total time spent in LLM pipeline (sec)
  std::vector<std::string> output;            // Model output vector for input prompts
  CategoryPerformanceResult  performance_results;
};

}  // namespace cil

#endif  // BENCHMARK_RESULT_H_
