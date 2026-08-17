#include "txt2txt_executor.h"

#include <log4cxx/logger.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <numeric>
#include <ranges>
#include <span>
#include <sstream>
#include <string_view>
#include <thread>

#include "common/llm/llm_config.h"
#include "llm_inference.h"
#include "llm_tools.h"
#include "prompt_file_reader.h"
#include "system_info_provider.h"

namespace fs = std::filesystem;
using namespace log4cxx;

namespace {

using Result = cil::infer::LLMInference::Result;
using ResultSpan = std::span<Result const>;
using ResultIter = ResultSpan::iterator;

using PromptWithTokensPair = std::pair<std::string, std::vector<uint32_t>>;

/**
 * @brief Represents a prompt and its tokens.
 * It doesn't own the containers for the prompt and the tokens.
 * User is responsible for ensuring that the containers are valid for the
 * lifetime of the PromptTokens object.
 *
 * @param prompt The prompt.
 * @param tokens The tokens.
 */
struct PromptWithTokensView {
  std::string_view prompt;
  std::span<uint32_t const> tokens;

  PromptWithTokensView() = default;

  explicit PromptWithTokensView(const PromptWithTokensPair& prompt_with_tokens)
      : prompt(prompt_with_tokens.first), tokens(prompt_with_tokens.second) {}

  PromptWithTokensView(std::string_view prompt,
                       std::span<uint32_t const> tokens)
      : prompt(prompt), tokens(tokens) {}
};

/**
 * @class LLMInferTask
 * @brief Represents a single step of the LLM inference run.
 *
 * This class represents a single step of the LLM inference run. It contains the
 * necessary information to run the inference step.
 */
struct LLMInferTask {
  PromptWithTokensView
      history_prompt;                // Prompt with history of the conversation
  PromptWithTokensView user_prompt;  // New user prompt and/or tools output
  PromptWithTokensView expected_output_prompt;  // Expected output prompt
  bool run_tools = false;
  bool is_warmup = false;
  Result result;  // Result of the inference task

  LLMInferTask(const PromptWithTokensView& history_prompt,
               const PromptWithTokensView& user_prompt,
               const PromptWithTokensView& expected_output_prompt,
               bool run_tools, bool is_warmup)
      : history_prompt(history_prompt),
        user_prompt(user_prompt),
        expected_output_prompt(expected_output_prompt),
        run_tools(run_tools),
        is_warmup(is_warmup) {}

  LLMInferTask(const PromptWithTokensView& user_prompt, bool run_tools,
               bool is_warmup)
      : user_prompt(user_prompt), run_tools(run_tools), is_warmup(is_warmup) {}
};

using InferTaskList = std::vector<LLMInferTask>;

template <typename T = double>
struct Statistics {
  T average;
  T min;
  T max;
};

template <typename Acc = double, std::input_iterator Iter>
Acc arithmean(Iter begin, Iter end, auto Op, Acc initial = Acc{0.0}) {
  if (begin == end) {
    return Acc{0.0};
  }
  Acc sum = initial;
  size_t cnt = 0;
  for (auto it = begin; it != end; ++it) {
    sum = sum + static_cast<Acc>(Op(*it));
    ++cnt;
  }
  return sum / static_cast<Acc>(cnt);
}

template <typename Acc = double, std::input_iterator Iter>
Acc geomean(Iter begin, Iter end, auto Op) {
  if (begin == end) {
    return Acc{0.0};
  }
  Acc s = Acc{1.0};
  size_t cnt = 0;
  for (auto it = begin; it != end; ++it) {
    s = s * static_cast<Acc>(Op(*it));
    ++cnt;
  }
  Acc r = std::pow(s, 1.0 / static_cast<Acc>(cnt));
  return r;
}

struct PromptPerformanceResult {
  using MillisecDuration = cil::infer::LLMInference::MillisecDuration;
  using SecondsDuration = std::chrono::duration<double>;
  using HardwareMetricStat = Statistics<double>;

  template <std::ranges::input_range R>
    requires std::same_as<std::ranges::range_value_t<R>, LLMInferTask>
  explicit PromptPerformanceResult(R&& measured_results) {
    if (std::ranges::empty(measured_results)) return;

    total_runs_count = std::ranges::size(measured_results);

    average_input_tokens = arithmean<double>(
        std::ranges::begin(measured_results),
        std::ranges::end(measured_results),
        [](const auto& task) { return task.result.input_tokens_count; });
    average_generated_tokens = arithmean<double>(
        std::ranges::begin(measured_results),
        std::ranges::end(measured_results),
        [](const auto& task) { return task.result.tokens.size(); });

    const auto sum_tools_time = std::accumulate(
        std::ranges::begin(measured_results),
        std::ranges::end(measured_results), MillisecDuration{0},
        [](MillisecDuration l, const auto& task) {
          return l + task.result.tools_time.value_or(MillisecDuration{0});
        });
    avg_tools_time = sum_tools_time / total_runs_count;

    const auto sum_duration = std::accumulate(
        std::ranges::begin(measured_results),
        std::ranges::end(measured_results), MillisecDuration{0},
        [](MillisecDuration l, const auto& task) {
          return l + std::chrono::duration_cast<MillisecDuration>(
                         task.result.end_time - task.result.start_time);
        });
    avg_duration = sum_duration / total_runs_count;

    // Collect Result iterators for TTFT / latency / memory helpers
    avg_ttft = GetAvgTTFT(std::ranges::begin(measured_results),
                          std::ranges::end(measured_results));
    avg_2nd_latency =
        GetAvgNthTokenLatency(std::ranges::begin(measured_results),
                              std::ranges::end(measured_results));

    const auto sum_avg_token_lat = std::accumulate(
        std::ranges::begin(measured_results),
        std::ranges::end(measured_results), MillisecDuration{0.0},
        [](MillisecDuration l, const auto& task) {
          return l + std::chrono::duration_cast<MillisecDuration>(
                         task.result.end_time - task.result.start_time) /
                         task.result.tokens.size();
        });
    avg_token_latency = sum_avg_token_lat / total_runs_count;

    const auto avg_expected_tokens = arithmean<double>(
        std::ranges::begin(measured_results),
        std::ranges::end(measured_results), [](const auto& task) {
          return static_cast<double>(task.expected_output_prompt.tokens.size());
        });

    avg_expected_duration =
        avg_expected_tokens *
            avg_2nd_latency +  // Duration for expected tokens generation
        avg_tools_time +       // Duration for tools execution
        avg_ttft;              // Average TTFT for all runs

    used_physical_memory =
        GetUsedMemoryStats(std::ranges::begin(measured_results),
                           std::ranges::end(measured_results));
    total_physical_memory =
        static_cast<double>(cil::SystemInfoProvider::Instance()
                                ->GetMemoryInfo()
                                .physical_total) /
        (1024.0 * 1024.0);  // in MB
  }

  PromptPerformanceResult(const PromptPerformanceResult& o) = default;
  PromptPerformanceResult(PromptPerformanceResult&& o) = default;

  std::string ToString() const {
    std::stringstream ss;
    ss << "[ Runs: " << total_runs_count
       << ", Average input: " << average_input_tokens
       << ", Average generated: " << average_generated_tokens
       << ", AVG duration (ms) " << std::fixed << std::setprecision(3)
       << avg_duration.count() << ", AVG expected duration (ms) " << std::fixed
       << std::setprecision(3) << avg_expected_duration.count()
       << ", AVG TTFT (ms) " << std::fixed << std::setprecision(3)
       << avg_ttft.count() << ", AVG latency (ms) " << std::fixed
       << std::setprecision(3) << avg_token_latency.count()
       << ", AVG 2nd latency (ms) " << std::fixed << std::setprecision(3)
       << avg_2nd_latency.count() << " ]" << std::endl;

    ss << "[ Used Physical Memory (MB): " << used_physical_memory.average
       << ", Total Physical Memory (MB): " << total_physical_memory << " ]";

    return ss.str();
  }

  static MillisecDuration GetAvgTTFT(auto begin, auto end) {
    MillisecDuration s{0.0};
    size_t cnt = 0;
    for (auto it = begin; it != end; ++it) {
      const auto& task = *it;
      if (task.result.timestamps.size() > 0) {
        cnt++;
        s += std::chrono::duration_cast<MillisecDuration>(
            task.result.timestamps[0] - task.result.start_time);
      }
    }
    return cnt != 0 ? MillisecDuration{s / cnt} : MillisecDuration{0.0};
  }

  MillisecDuration GetAvgNthTokenLatency(auto begin, auto end,
                                         size_t offset = 0) const {
    MillisecDuration s{0.0};
    size_t cnt = 0;
    for (auto it = begin; it != end; ++it) {
      const auto& task = *it;
      if (task.result.timestamps.size() > offset + 1) {
        cnt++;
        s += std::chrono::duration_cast<MillisecDuration>(
                 task.result.end_time - task.result.timestamps[offset]) /
             (task.result.timestamps.size() - offset - 1);
      }
    }
    return cnt != 0 ? MillisecDuration{s / cnt} : MillisecDuration{0.0};
  }

  HardwareMetricStat GetUsedMemoryStats(auto begin, auto end) const {
    HardwareMetricStat stat{0.0, std::numeric_limits<double>::max(), 0.0};
    size_t count = 0;
    for (auto it = begin; it != end; ++it) {
      const auto& task = *it;
      for (const auto& [mem_info, _] : task.result.hw_info_records) {
        double used_mem = static_cast<double>(mem_info.physical_total -
                                              mem_info.physical_available) /
                          (1024.0 * 1024.0);
        stat.average += used_mem;
        stat.min = std::min(stat.min, used_mem);
        stat.max = std::max(stat.max, used_mem);
        count++;
      }
    }
    if (count > 0) {
      stat.average /= static_cast<double>(count);
    }
    return stat;
  }

  bool has_empty_output = true;
  std::string category;
  size_t total_runs_count = 0;
  double average_input_tokens = 0;
  double average_generated_tokens = 0;
  MillisecDuration avg_duration{0.0};
  MillisecDuration avg_ttft{0.0};
  MillisecDuration avg_token_latency{0.0};
  MillisecDuration avg_2nd_latency{0.0};

  // Agentic-only metrics (zero for non-agentic scenarios).
  MillisecDuration avg_expected_duration{0.0};
  MillisecDuration avg_tools_time{0.0};

  double total_physical_memory = 0.0;
  HardwareMetricStat used_physical_memory{0.0, 0.0, 0.0};
};

}  // namespace

namespace cil::infer {

void Txt2TxtExecutor::RunScenario(EP ep, const nlohmann::json& settings) {
  std::string model_file_path = fs::path(model_path_).string();

  std::unique_ptr<LLMInference> inference;
  try {
    inference = std::make_unique<LLMInference>(
        scenario_name_, model_base_name_, model_file_path, deps_dir_, ep,
        settings, library_path_, executor_logger_->getName());
  } catch (const std::exception& e) {
    benchmark_result_.error_message = e.what();
    status_ = Status::kFailed;
    return;
  }

  auto error_message = inference->GetErrorMessage();
  if (!error_message.empty()) {
    benchmark_result_.error_message = inference->GetErrorMessage(false);
    benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
    status_ = Status::kFailed;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(task_description_mutex_);
    task_description_ = ep_name_ + " on " + inference->GetDeviceType() +
                        " for " + display_name_;
  }

  if (status_ == Status::kCanceled) {
    return;
  }

  status_ = Status::kRunning;

  // Pre-compute the grand total of inference sessions across all input files
  // so progress_ has a stable denominator throughout the run. Without this,
  // sessions_count grows during each input's task-construction pass while
  // current_session_index has already advanced from prior inputs — making
  // progress_ visibly drop at the start of every input file.
  int sessions_count = 0;
  for (const auto& path : input_paths_) {
    const int input_prompts = PromptFileReader::CountPromptsInFile(path);
    if (!is_agentic_) {
      sessions_count += (iterations_ + iterations_warmup_) * input_prompts;
    } else if (input_prompts >= 3 && input_prompts % 2 == 1) {
      // Mirrors the agentic build below: per iteration, one warmup task plus
      // (input_prompts - 1) / 2 inference tasks.
      sessions_count += iterations_ * (1 + (input_prompts - 1) / 2);
    }
  }
  int current_session_index = 0;  // Current session index to update progress

  int all_correct_predictions_count = 0;
  int all_predictions_count = 0;

  size_t all_prompt_index = 0;

  std::string last_model_config;
  bool model_initialized = false;
  bool low_tokens_count = false;

  LOG4CXX_INFO(
      executor_logger_,
      "Running " << ep_name_ << " on " << inference->GetDeviceType() << " for "
                 << display_name_ << " with " << iterations_ << " iterations."
                 << " The first iteration will be used to warm up the log.");

  const auto& required_categories = BenchmarkResult::kLLMRequiredCategories;
  std::unordered_map<std::string, std::vector<PromptPerformanceResult>>
      prompt_perf_results;  // map of category to vector of prompt performance
                            // results

  std::vector<std::vector<size_t>> skipped_prompts;
  bool has_skipped_prompts = false;
  bool has_skipped_required_prompts = false;

  /**************************************************
   * Run inference for each input file.
   *************************************************/
  for (const std::string& input_path : input_paths_) {
    if (status_ == Status::kCanceled) return;

    // Read input json to get input prompts using nlohmann::json
    struct InputFileData {
      std::string category;
      std::string model_config;
      size_t max_length;
      nlohmann::json input_json;
    };

    const auto input_file_data = [&]() {
      std::ifstream input_file(input_path);
      nlohmann::json input_json;
      input_file >> input_json;
      input_file.close();

      std::string category = input_json.contains("category")
                                 ? input_json["category"].get<std::string>()
                                 : std::string{"Unknown"};
      std::string model_config = input_json["model_config"].dump();
      const size_t max_length =
          input_json["model_config"]["search"]["max_length"].get<size_t>();

      // Load prompts from files if prompt_files is true
      if (input_json.value("prompt_files", false)) {
        const fs::path base_dir = fs::path(input_path).parent_path();
        auto load_file = [&](const std::string& rel_path) {
          const fs::path full = base_dir / rel_path;
          std::ifstream f(full, std::ios::binary);
          if (!f.is_open()) {
            LOG4CXX_ERROR(executor_logger_, "prompt_files: failed to open "
                                                << full.string()
                                                << " (referenced from "
                                                << input_path << ")");
            return std::string{};
          }
          std::ostringstream ss;
          ss << f.rdbuf();
          return ss.str();
        };

        for (auto& prompt : input_json["prompts"]) {
          if (prompt.is_string()) {
            prompt = load_file(prompt.get<std::string>());
          } else if (prompt.is_object()) {
            for (auto& [key, val] : prompt.items()) {
              if (val.is_string()) {
                val = load_file(val.get<std::string>());
              }
            }
          }
        }
      }  // prompt_files == true

      return InputFileData{std::move(category), std::move(model_config),
                           max_length, std::move(input_json)};
    }();

    const bool apply_template =
        input_file_data.input_json.value("apply_chat_template", false);

    const std::optional<bool> file_enable_thinking =
        [&]() -> std::optional<bool> {
      if (const auto& j = input_file_data.input_json;
          j.contains("enable_thinking") && j["enable_thinking"].is_boolean())
        return j["enable_thinking"].get<bool>();
      return std::nullopt;
    }();

    /**************************************************
     * Tokenize input prompts.
     * prompt_tokens SHALL be immutable for the duration of this function.
     *************************************************/
    const std::vector<PromptWithTokensPair> input_prompts_with_tokens = [&]() {
      std::vector<PromptWithTokensPair> prompts_with_tokens;
      for (const auto& prompt : input_file_data.input_json["prompts"]) {
        std::string prompt_str;
        bool templated = false;

        if (prompt.is_object()) {
          nlohmann::json messages = nlohmann::json::array();
          for (const char* key : {"system", "user", "assistant", "agent"}) {
            if (prompt.contains(key) && prompt[key].is_string()) {
              messages.push_back(
                  {{"role", key}, {"content", prompt[key].get<std::string>()}});
            }
          }
          if (messages.empty()) {
            LOG4CXX_ERROR(executor_logger_,
                          "Object-form prompt contains no recognized role "
                          "keys (system, user, assistant, agent)");
            return std::vector<PromptWithTokensPair>{};
          }

          // No gen prompt when the entry ends on a pre-recorded assistant turn.
          const std::string last_role = messages.back()["role"];
          const bool add_gen_prompt =
              (last_role != "assistant" && last_role != "agent");

          // Per-prompt enable_thinking overrides the file-level default.
          std::optional<bool> enable_thinking = file_enable_thinking;
          if (prompt.contains("enable_thinking") &&
              prompt["enable_thinking"].is_boolean()) {
            enable_thinking = prompt["enable_thinking"].get<bool>();
          }

          if (tokenizer_->HasChatTemplate()) {
            std::string rendered = tokenizer_->ApplyChatTemplate(
                messages.dump(-1, ' ', false,
                              nlohmann::json::error_handler_t::replace),
                add_gen_prompt, enable_thinking);
            if (!rendered.empty()) {
              prompt_str = std::move(rendered);
              templated = true;
            }
          }
          if (!templated) {
            LOG4CXX_WARN(
                executor_logger_,
                "Object-form prompt provided but tokenizer has no chat "
                "template; falling back to concatenated text");
            for (const auto& m : messages) {
              prompt_str += m["content"].get<std::string>() + "\n\n";
            }
          }
        } else {
          prompt_str = prompt.get<std::string>();
          templated = tokenizer_->DetectChatTemplateInText(prompt_str);

          if (!templated && apply_template && tokenizer_->HasChatTemplate()) {
            nlohmann::json messages = nlohmann::json::array(
                {{{"role", "user"}, {"content", prompt_str}}});
            std::string rendered = tokenizer_->ApplyChatTemplate(
                messages.dump(-1, ' ', false,
                              nlohmann::json::error_handler_t::replace),
                true, file_enable_thinking);
            if (!rendered.empty()) {
              prompt_str = std::move(rendered);
              templated = true;
            }
          }
        }

        std::vector<uint32_t> input_tokens;
        if (!TokenizeOrFail(prompt_str, input_tokens,
                            /*add_special_tokens=*/!templated)) {
          return std::vector<PromptWithTokensPair>{};  // Empty vector to
                                                       // indicate failure
        }
        prompts_with_tokens.emplace_back(std::move(prompt_str),
                                         std::move(input_tokens));
      }
      return prompts_with_tokens;
    }();

    if (input_prompts_with_tokens.empty()) {
      LOG4CXX_ERROR(executor_logger_, "Failed to tokenize input prompts");
      return;
    }

    /**************************************************
     * Create inference tasks for each prompt.
     *
     * Legacy: InferTaskList contains multiple inference tasks for the same
     *prompt. Agentic: InferTaskList contains multiple inference tasks for
     *different prompts in a series, representing a single conversation/agentic
     *turn.
     *************************************************/
    std::vector<InferTaskList> infer_tasks_per_input;
    if (!is_agentic_) {
      // ******************** NON-AGENTIC INFERENCE TASKS ********************
      LOG4CXX_DEBUG(executor_logger_, "Creating non-agentic inference tasks");
      infer_tasks_per_input.resize(input_prompts_with_tokens.size());
      // Repeat each prompt for the number of iterations and warmup steps
      for (size_t i = 0; i < input_prompts_with_tokens.size(); ++i) {
        for (size_t j = 0; j < iterations_ + iterations_warmup_; ++j) {
          // Emplace user prompt only for non agentic tasks
          infer_tasks_per_input[i].emplace_back(
              PromptWithTokensView{input_prompts_with_tokens[i]},
              false /* run_tools */, j < iterations_warmup_ /* is_warmup */);
        }
      }
    } else {
      // ******************** AGENTIC INFERENCE TASKS ********************
      LOG4CXX_DEBUG(
          executor_logger_,
          std::string{"Creating agentic inference tasks, tools execution: "} +
              (tools_execution_ ? "true" : "false"));
      if (input_prompts_with_tokens.size() < 3 ||
          input_prompts_with_tokens.size() % 2 == 0) {
        LOG4CXX_ERROR(executor_logger_,
                      "Invalid number of prompts for agentic inference. Must "
                      "be at least 3 and odd.");
        status_ = Status::kFailed;
        return;
      }
      infer_tasks_per_input.resize(iterations_);
      // Repeat series of prompts for the number of iterations
      // First prompt for warmup
      for (size_t j = 0; j < iterations_; ++j) {
        InferTaskList& infer_tasks = infer_tasks_per_input[j];

        // Add warmup task for the first prompt
        infer_tasks.emplace_back(
            PromptWithTokensView{input_prompts_with_tokens[0]},
            false /* run_tools */, true /* is_warmup */);
        LOG4CXX_DEBUG(executor_logger_,
                      " - warmup task added. tokens: "
                          << input_prompts_with_tokens[0].second.size());

        // Add system prompt, user prompt, and expected output prompt for each
        // iteration Skip the last prompt as it is the expected output prompt
        for (size_t i = 0; i < input_prompts_with_tokens.size() - 1; i += 2) {
          const auto& task = infer_tasks.emplace_back(
              /* Model -> History prompt */
              i == 0 ? PromptWithTokensView{}
                     : PromptWithTokensView{input_prompts_with_tokens[i]},
              /* Model <- System/User prompt or tools output */
              PromptWithTokensView{input_prompts_with_tokens[i + 1]},
              /* Model -> Expected output prompt */
              PromptWithTokensView{input_prompts_with_tokens[i + 2]},
              tools_execution_ /* run_tools */, false /* is_warmup */);
          LOG4CXX_DEBUG(executor_logger_,
                        " - inference task added. history: "
                            << task.history_prompt.tokens.size() << ", user: "
                            << task.user_prompt.tokens.size() << ", expected: "
                            << task.expected_output_prompt.tokens.size());
        }
      }
    }

    // Fix max total length for the model config.
    // Legacy feeds one prompt per inference (largest input = largest single
    // prompt); agentic feeds the growing conversation into every turn, so the
    // largest input is the full accumulated history (all prompts except the
    // warmup entry and the final expected output, which is never fed back).
    size_t max_input_tokens = 0;
    if (!input_prompts_with_tokens.empty()) {
      if (is_agentic_) {
        for (size_t k = 1; k + 1 < input_prompts_with_tokens.size(); ++k)
          max_input_tokens += input_prompts_with_tokens[k].second.size();
      } else {
        max_input_tokens =
            std::ranges::max(input_prompts_with_tokens |
                             std::views::transform([](const auto& p) {
                               return p.second.size();
                             }));
      }
    }
    auto model_config_json = input_file_data.input_json["model_config"];
    int max_gen = model_config_json["search"].value("max_length", 1024);
    int ctx_len = cil::infer::LlmConfig::GetContextLength(model_config_json);

    // Clamp max_length so that input + max_length <= context window. Otherwise
    // the IHV re-checks input + max_length and trips its context guard.
    const int room = ctx_len - static_cast<int>(max_input_tokens);
    max_gen = std::max(0, std::min(max_gen, room));
    if (max_gen == 0) {
      LOG4CXX_ERROR(executor_logger_,
                    "Input tokens (" << max_input_tokens
                                     << ") leave no room for generation within "
                                        "context_length ("
                                     << ctx_len << ")");
    }
    model_config_json["search"]["max_length"] = max_gen;

    const int max_total =
        std::min(static_cast<int>(max_input_tokens) + max_gen, ctx_len);
    model_config_json["search"]["max_total_length"] = max_total;
    std::string model_config = model_config_json.dump();

    bool is_required_category =
        std::ranges::find(required_categories, input_file_data.category) !=
        required_categories.end();
    std::vector<size_t> skipped_prompts_for_input;
    size_t prompt_index = 0;

    for (size_t group_idx = 0; group_idx < infer_tasks_per_input.size();
         ++group_idx) {
      // 1. Initialize the model if not initialized yet or if the model config
      // has
      //    changed. Also re-initialize the model if agentic mode is enabled.
      // 2. Re-initialize the model if agentic mode is enabled
      //    to make sure the context is cleared and reset before
      //    running the next agentic flow.
      if (!model_initialized) {
        inference->Init(model_config);
        last_model_config = model_config;
      } else if (model_config != last_model_config || is_agentic_) {
        inference->Deinit();
        inference->Init(model_config);
        last_model_config = model_config;
      }

      error_message = inference->GetErrorMessage();
      if (!error_message.empty()) {
        benchmark_result_.error_message = inference->GetErrorMessage(false);
        benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
        status_ = Status::kFailed;
        return;
      }

      model_initialized = true;

      auto& infer_tasks = infer_tasks_per_input[group_idx];
      prompt_index = all_prompt_index;

      bool is_success = true;

      auto check_error = [&]() {
        if (const auto& infer_error_message = inference->GetErrorMessage();
            !infer_error_message.empty()) {
          benchmark_result_.error_message = inference->GetErrorMessage(false);
          benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
          is_success = false;
          return true;
        }
        return false;
      };

      /***************************************************
       * Run inference for each task in the infer_tasks list.
       * For non-agentic tasks, run inference with user prompt tokens.
       * For agentic tasks, run inference with history tokens.
       * History tokens are updated with each new prompt.
       ****************************************************/
      size_t task_iter = 0;
      std::vector<uint32_t> history_tokens;
      for (; task_iter < infer_tasks.size(); ++task_iter) try {
          auto& task = infer_tasks[task_iter];

          if (task.is_warmup && !is_agentic_) {
            LOG4CXX_DEBUG(executor_logger_,
                          "Prompt: " + std::string{task.user_prompt.prompt});
            LOG4CXX_DEBUG(executor_logger_,
                          "Category: " + input_file_data.category);
          }

          if (status_ == Status::kCanceled) {
            LOG4CXX_DEBUG(executor_logger_, "Canceled");
            inference->Deinit();
            return;
          }

          inference->Prepare();
          if (check_error()) break;

          std::this_thread::sleep_for(inference_delay_);

          // Run inference with appropriate tokens for agentic and legacy tasks
          task.result = [&]() {
            if (!is_agentic_) {
              // Run inference with user prompt tokens for legacy tasks
              return inference->Run(task.user_prompt.tokens);
            } else {
              LOG4CXX_DEBUG(executor_logger_, "Running agentic inference");
              LOG4CXX_DEBUG(executor_logger_,
                            "  System prompt: " +
                                std::string{task.history_prompt.prompt});
              LOG4CXX_DEBUG(
                  executor_logger_,
                  "  User prompt: " + std::string{task.user_prompt.prompt});
              if (task.is_warmup) {
                // Run inference with user prompt tokens for warmup steps
                return inference->Run(task.user_prompt.tokens);
              }
              // Add system prompt and user prompt to history tokens
              history_tokens.insert(history_tokens.end(),
                                    task.history_prompt.tokens.begin(),
                                    task.history_prompt.tokens.end());
              history_tokens.insert(history_tokens.end(),
                                    task.user_prompt.tokens.begin(),
                                    task.user_prompt.tokens.end());
              // Run inference with history tokens
              return inference->Run(history_tokens);
            }
          }();

          task.result.category = input_file_data.category;
          if (task.run_tools) {
            LOG4CXX_DEBUG(executor_logger_, "Running tools");
            const auto tool_results = RunTools(
                task.expected_output_prompt.prompt,
                fs::path(input_path).parent_path().string(), executor_logger_);
            if (const bool all_succeeded = std::ranges::all_of(
                    tool_results,
                    [](const auto& r) { return r.result.success; });
                !all_succeeded) {
              LOG4CXX_ERROR(executor_logger_, "One or more tool calls failed");
              is_success = false;
              error_message = "One or more tool calls failed";
              break;
            }

            const LLMInference::MillisecDuration total_tools_time =
                std::accumulate(tool_results.begin(), tool_results.end(),
                                LLMInference::MillisecDuration{0},
                                [](LLMInference::MillisecDuration l,
                                   const auto& r) { return l + r.duration; });
            task.result.tools_time = total_tools_time;
            LOG4CXX_DEBUG(
                executor_logger_,
                "Tools time: " + std::to_string(total_tools_time.count()));
          }

          bool infer_error = check_error();
          inference->Reset();
          if (infer_error || check_error()) break;

          std::string decoded_output;
          if (!DetokenizeOrFail(task.result.tokens, decoded_output)) {
            inference->Deinit();
            return;
          }

          if (const bool log_output = (!is_agentic_ && task_iter == 0) ||
                                      (is_agentic_ && !task.is_warmup);
              log_output) {
            LOG4CXX_DEBUG(executor_logger_,
                          "Input: " + std::string{task.user_prompt.prompt});
            LOG4CXX_DEBUG(executor_logger_, "Output: " + decoded_output);
            benchmark_result_.output.emplace_back(decoded_output);
            benchmark_result_.input_prompts.emplace_back(
                std::string{task.user_prompt.prompt});
            benchmark_result_.input_categories.emplace_back(
                input_file_data.category);
            benchmark_result_.input_history.emplace_back(
                is_agentic_ ? std::string{task.history_prompt.prompt}
                            : std::string{});
          }

          ++current_session_index;
          progress_ = current_session_index * 100 / sessions_count;
        } catch (const std::exception& e) {
          LOG4CXX_ERROR(executor_logger_, "Exception during task iteration "
                                              << task_iter << ": " << e.what());
          is_success = false;
          error_message = std::string("Exception: ") + e.what();
          try {
            inference->Reset();
          } catch (...) {
          }
          break;
        }  // tasks

      LOG4CXX_INFO(executor_logger_,
                   "Validating prompt results " + std::to_string(prompt_index) +
                       "/" + std::to_string(expected_tokens_.size()));

      if (expected_tokens_.size() <= prompt_index) {
        LOG4CXX_WARN(executor_logger_, "Prompt results are not provided.");
      } else {
        const std::vector<uint32_t>& expected_output_ids =
            expected_tokens_[prompt_index];
        size_t correct_predictions_count =
            std::count_if(infer_tasks.begin(), infer_tasks.end(),
                          [&expected_output_ids](const auto& t) {
                            return t.result.tokens == expected_output_ids;
                          });

        LOG4CXX_DEBUG(
            executor_logger_,
            "Correct results: " << correct_predictions_count << "/"
                                << infer_tasks.size() << " - Verified on "
                                << expected_output_ids.size() << " tokens");

        all_correct_predictions_count += correct_predictions_count;
      }
      ++prompt_index;
      all_predictions_count += infer_tasks.size();

      bool empty_tokens = std::ranges::any_of(
          infer_tasks, [](const auto& t) { return t.result.tokens.empty(); });

      if (!empty_tokens) {
        low_tokens_count = low_tokens_count ||
                           std::ranges::any_of(infer_tasks, [](const auto& t) {
                             return t.result.tokens.size() < 2;
                           });
      }

      all_prompt_index = prompt_index;
      current_prompt_ = static_cast<int>(is_agentic_ ? prompt_index / iterations_
                                                     : prompt_index);

      if (!is_success ||
          empty_tokens /*|| (is_agentic_ && low_tokens_count)*/) {
        if ((skip_failed_required_prompts_ || !is_required_category) &&
            !is_agentic_) {
          skipped_prompts_for_input.push_back(group_idx);
          has_skipped_prompts = true;
          if (is_required_category) has_skipped_required_prompts = true;
          current_session_index +=
              static_cast<int>(infer_tasks.size() - task_iter);
          progress_ = current_session_index * 100 / sessions_count;
          inference->ClearErrorMessage();
          inference->Deinit();
          inference->Init(model_config);
          continue;
        } else {
          status_ = Status::kFailed;
          if (benchmark_result_.error_message.empty()) {
            benchmark_result_.error_message =
                "There is empty output in the result.";
          }
          inference->Deinit();
          return;
        }
      }

      // For non-agentic, aggregate per-prompt over the measured iterations,
      // dropping the warmup prefix. Agentic aggregates later per turn index.
      if (!is_agentic_) {
        const auto& aggregated_results =
            prompt_perf_results[input_file_data.category].emplace_back(
                infer_tasks | std::views::drop(iterations_warmup_));
        LOG4CXX_DEBUG(executor_logger_, "Prompt timing results: " +
                                            aggregated_results.ToString());
      }
    }  // groups

    if (is_agentic_) {
      // Average metrics across outer iterations; each span element is one full
      // agentic run
      LOG4CXX_DEBUG(executor_logger_, "Calculating agentic benchmark with "
                                          << input_prompts_with_tokens.size()
                                          << " sessions");
      if (!infer_tasks_per_input.empty()) {
        const size_t tasks_per_iteration =
            1 + (input_prompts_with_tokens.size() - 1) / 2;
        for (size_t task_idx = 0; task_idx < tasks_per_iteration; ++task_idx) {
          // Check if all tasks in the iteration are warmup
          if (const bool all_warmup =
                  std::ranges::all_of(infer_tasks_per_input,
                                      [task_idx](const auto& tasks) {
                                        return tasks[task_idx].is_warmup;
                                      });
              all_warmup) {
            LOG4CXX_DEBUG(executor_logger_, "All tasks in the iteration "
                                                << task_idx
                                                << " are warmup. Skipping...");
            continue;
          }

          // Accumulate results for each iteration
          const auto& aggregated_results =
              prompt_perf_results[input_file_data.category].emplace_back(
                  std::ranges::views::iota(size_t{0},
                                           infer_tasks_per_input.size()) |
                  std::ranges::views::transform(
                      [task_idx, &infer_tasks_per_input](
                          size_t iteration_index) -> const LLMInferTask& {
                        return infer_tasks_per_input[iteration_index][task_idx];
                      }));

          LOG4CXX_DEBUG(executor_logger_, "Agentic Prompt timing results: "
                                              << task_idx << " - "
                                              << aggregated_results.ToString());
        }
      }
    }

    skipped_prompts.push_back(skipped_prompts_for_input);

  }  // input path

  if (model_initialized) {
    inference->Deinit();
  }

  if (status_ == Status::kRunning) {
    status_ = Status::kCompleted;
  }

  benchmark_result_.is_llm_benchmark = true;
  benchmark_result_.benchmark_success = status_ == Status::kCompleted;
  benchmark_result_.device_type = inference->GetDeviceType();

  constexpr double kMsToSecRatio = 1e3;

  CategoryPerformanceResult per_category_perf_results{};

  PromptPerformanceResult::MillisecDuration avg_duration_sum{0.0};
  size_t avg_duration_cnt{0};
  for (const auto& [category, perf_results] : prompt_perf_results) {
    per_category_perf_results[category].is_agentic = is_agentic_;

    avg_duration_sum += std::accumulate(
        perf_results.begin(), perf_results.end(),
        PromptPerformanceResult::MillisecDuration{0.0},
        [](auto a, const auto& r) { return a + r.avg_duration; });
    avg_duration_cnt += perf_results.size();

    per_category_perf_results[category].average_generated_tokens =
        static_cast<uint64_t>(arithmean<double>(
            perf_results.begin(), perf_results.end(),
            [](const auto& r) { return r.average_generated_tokens; }));

    // For agentic, the "input tokens" of interest is the full context window
    // size at the final turn (perf_results.back()), not the average across
    // turns of growing history.
    per_category_perf_results[category].average_input_tokens =
        static_cast<uint64_t>(
            is_agentic_ && !perf_results.empty()
                ? perf_results.back().average_input_tokens
                : arithmean<double>(
                      perf_results.begin(), perf_results.end(),
                      [](const auto& r) { return r.average_input_tokens; }));

    if (!low_tokens_count) {
      per_category_perf_results[category].token_generation_rate =
          arithmean<double>(perf_results.begin(), perf_results.end(),
                            [](const auto& r) {
                              return kMsToSecRatio / r.avg_2nd_latency.count();
                            });
    }

    per_category_perf_results[category].time_to_first_token_duration =
        arithmean<double>(
            perf_results.begin(), perf_results.end(),
            [](const auto& r) { return r.avg_ttft.count() / kMsToSecRatio; });

    per_category_perf_results[category].average_expected_duration =
        std::accumulate(perf_results.begin(), perf_results.end(), 0.0,
                        [](double a, const auto& r) {
                          return a + r.avg_expected_duration.count() /
                                         kMsToSecRatio;
                        });

    per_category_perf_results[category].average_tools_duration =
        std::accumulate(perf_results.begin(), perf_results.end(), 0.0,
                        [](double a, const auto& r) {
                          return a + r.avg_tools_time.count() / kMsToSecRatio;
                        });
  }

  if (has_skipped_prompts)
    LOG4CXX_WARN(executor_logger_,
                 "Some prompts were skipped due to empty output.");

  bool calculate_overall_results = true;
  if (has_skipped_required_prompts) {
    LOG4CXX_WARN(executor_logger_,
                 "Since some required prompts were skipped, overall "
                 "performance results will not be calculated.");
    calculate_overall_results = false;
  }

  CategoryPerformanceResult required_perf_results{};
  if (calculate_overall_results) {
    for (const auto& category : required_categories) {
      if (auto it = per_category_perf_results.find(category);
          it != per_category_perf_results.end()) {
        required_perf_results[it->first] = it->second;
      } else {
        calculate_overall_results = false;
        LOG4CXX_WARN(executor_logger_,
                     "Some required categories are missing. Overall "
                     "performance results will not be calculated.");
        break;
      }
    }
  }

  if (calculate_overall_results) {
    PerformanceResult overall_performance_results{};

    overall_performance_results.average_generated_tokens =
        static_cast<uint64_t>(arithmean<double>(
            required_perf_results.begin(), required_perf_results.end(),
            [](const auto& r) { return r.second.average_generated_tokens; }));
    overall_performance_results.average_input_tokens =
        static_cast<uint64_t>(arithmean<double>(
            required_perf_results.begin(), required_perf_results.end(),
            [](const auto& r) { return r.second.average_input_tokens; }));
    overall_performance_results.time_to_first_token_duration = geomean<double>(
        required_perf_results.begin(), required_perf_results.end(),
        [](const auto& r) { return r.second.time_to_first_token_duration; });
    overall_performance_results.token_generation_rate = geomean<double>(
        required_perf_results.begin(), required_perf_results.end(),
        [](const auto& r) { return r.second.token_generation_rate; });

    if (!per_category_perf_results
             .try_emplace(BenchmarkResult::kLLMOverallCategory,
                          overall_performance_results)
             .second) {
      LOG4CXX_ERROR(executor_logger_,
                    "'Overall' category already present in the list. Prompts "
                    "shall not have it.");
      status_ = Status::kFailed;
    }
  }

  benchmark_result_.duration =
      (avg_duration_sum / avg_duration_cnt).count() / kMsToSecRatio;
  benchmark_result_.performance_results = per_category_perf_results;
  benchmark_result_.results_verified =
      all_correct_predictions_count == all_predictions_count;

  benchmark_result_.skipped_prompts = std::move(skipped_prompts);
}

}  // namespace cil::infer
