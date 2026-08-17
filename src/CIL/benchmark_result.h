#ifndef BENCHMARK_RESULT_H_
#define BENCHMARK_RESULT_H_

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cil {

struct PerformanceResult {
  double time_to_first_token_duration = 0.;  // Geomean TTFT (sec)
  double token_generation_rate = 0.0;        // 2+ token/second rate
  double average_expected_duration = 0.0;    // Average expected duration (sec)
  double average_tools_duration = 0.0;       // Average tools duration (sec)
  bool is_agentic = false;                   // Whether the benchmark is agentic
  uint64_t average_input_tokens =
      0;  // Average input tokens provided to LLM pipeline
  uint64_t average_generated_tokens =
      0;  // Average tokens generated for all runs
};

struct SystemInfo {
  std::string os_name;
  size_t ram = 0;
  std::string cpu_model;
  std::string cpu_architecture;
  std::string gpu_name;
  size_t gpu_ram = 0;
};

using CategoryPerformanceResult =
    std::unordered_map<std::string, PerformanceResult>;

struct BenchmarkResult {
  std::string config_file_name;
  std::string config_file_hash;
  std::string config_file_comment;
  std::string scenario_name;
  std::string model_base_name;
  std::string model_path;
  std::vector<std::string> asset_paths;
  std::vector<std::string> data_paths;
  bool has_non_base_prompts;
  std::string results_file;
  std::string model_name;
  std::string model_file_name;
  std::vector<std::string> asset_file_names;
  std::vector<std::string> data_file_names;
  std::string execution_provider_name;
  nlohmann::json ep_configuration;
  SystemInfo system_info;
  int iterations = 0;
  int iterations_warmup = 0;
  bool benchmark_success = false;
  std::string benchmark_start_time;
  double duration = 0.0;  // Average time per iteration
  bool results_verified = false;
  bool config_verified = false;
  std::string config_category;
  std::string benchmark_duration;
  std::string error_message;
  std::string ep_error_messages;
  std::string device_type;
  std::vector<int> labels;
  std::vector<std::string> label_strings;
  std::vector<double> probability_percents;
  std::vector<std::vector<size_t>>
      skipped_prompts;  // Prompts that were skipped

  // Image generation fields
  bool is_image_benchmark = false;
  double images_per_minute = 0.0;
  double steps_per_second = 0.0;
  double avg_end_to_end_seconds = 0.0;
  double min_end_to_end_seconds = 0.0;
  double max_end_to_end_seconds = 0.0;
  double stdev_end_to_end_seconds = 0.0;
  double first_step_seconds = 0.0;
  double setup_load_seconds = 0.0;
  double setup_warmup_seconds = 0.0;
  std::unordered_map<std::string, double> setup_opt_seconds;
  std::unordered_map<std::string, std::vector<double>> submodule_timings_ms;
  uint32_t image_width = 0;
  uint32_t image_height = 0;
  uint32_t num_inference_steps = 0;
  std::vector<std::string> output_image_paths;

  // LLM stuffs
  bool is_llm_benchmark = false;
  double total_inference_duration =
      0.0;                          // Total time spent in LLM pipeline (sec)
  std::vector<std::string> output;  // Model output vector for input prompts
  CategoryPerformanceResult performance_results;

  // I/O viewer inputs (paired with `output` for LLM scenarios or
  // `output_image_paths` for image scenarios; one entry per response/image).
  // For agentic runs each entry holds the most recent user turn for the step.
  std::vector<std::string> input_prompts;
  std::vector<std::string> input_categories;
  // For agentic LLM steps, the conversation history up to (but not including)
  // the corresponding user turn; empty for non-agentic and image runs.
  std::vector<std::string> input_history;

  static inline const std::string kLLMOverallCategory = "Overall";
  static inline const std::vector<std::string> kLLMRequiredCategories = {
      "Content Generation", "Creative Writing", "Structured Text",
      "Code Analysis", "Summarization, Intermediate"};
  static inline const std::vector<std::string> kLLMExtendedCategories = {
      "Summarization, Substantial"};

  static inline const std::vector<std::string> kImageRequiredCategories = {
      "Experimental"};
};

}  // namespace cil

#endif  // BENCHMARK_RESULT_H_
