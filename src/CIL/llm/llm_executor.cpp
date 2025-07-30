#include "llm_executor.h"

#include <log4cxx/logger.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>

#include "../utils.h"
#include "json_schema.h"
#include "llm_inference.h"
#include "scenario_data_provider.h"
#include "tokenizer/tfmtok.h"

using namespace log4cxx;

LoggerPtr llama2_executor_logger(Logger::getLogger("Llama2Executor"));
LoggerPtr llama3_executor_logger(Logger::getLogger("Llama3Executor"));
LoggerPtr phi3_5_executor_logger(Logger::getLogger("Phi3_5Executor"));
LoggerPtr phi4_executor_logger(Logger::getLogger("Phi4Executor"));

namespace {

// Helper functions
using Result = cil::infer::LLMInference::Result;
using ResultSpan = std::span<Result const>;
using ResultIter = ResultSpan::iterator;

template <typename Acc = double, typename Iter>
Acc arithmean(Iter begin, Iter end, auto Op) {
  if (begin == end) {
    return Acc{0.0};
  }
  size_t cnt = std::distance(begin, end);
  Acc sum =
      std::accumulate(begin, end, Acc{0.0},
                      [&](Acc l, const auto& r) -> Acc { return l + Op(r); });
  return sum / static_cast<Acc>(cnt);
}

template <typename Acc = double, typename Iter>
Acc geomean(Iter begin, Iter end, auto Op) {
  if (begin == end) {
    return Acc{0.0};
  }
  size_t cnt = std::distance(begin, end);
  Acc s =
      std::accumulate(begin, end, Acc{1.0},
                      [&](Acc l, const auto& r) -> Acc { return l * Op(r); });
  Acc r = std::pow(s, 1.0 / static_cast<Acc>(cnt));
  return r;
}

template <typename Op, typename T>
bool span_values_all(std::span<T const> lhs, std::span<T const> rhs) {
  bool r = true;
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (int i = 0; i < lhs.size(); i++) {
    r = r && Op()(lhs[i], rhs[i]);
  }
  return r;
}

struct PromptPerformanceResult {
  using MillisecDuration = cil::infer::LLMInference::MillisecDuration;
  using SecondsDuration = std::chrono::duration<double>;

  PromptPerformanceResult(std::span<Result const> prompt_results,
                          size_t warmup_count = 1) {
    has_empty_output =
        std::any_of(prompt_results.begin(), prompt_results.end(),
                    [](const auto& r) { return r.tokens.size() == 0; });

    total_runs_count = prompt_results.size();

    average_input_tokens = arithmean<double>(
        prompt_results.begin() + warmup_count, prompt_results.end(),
        [](const auto& r) { return r.input_tokens_count; });
    average_generated_tokens = arithmean<double>(
        prompt_results.begin() + warmup_count, prompt_results.end(),
        [](const auto& r) { return r.tokens.size(); });

    // Calculate prompt metrics
    const auto sum_duration = std::accumulate(
        prompt_results.begin() + warmup_count, prompt_results.end(),
        MillisecDuration{0}, [](MillisecDuration l, const auto& r) {
          return l + std::chrono::duration_cast<MillisecDuration>(r.end_time -
                                                                  r.start_time);
        });
    avg_duration = sum_duration / (prompt_results.size() - warmup_count);

    avg_ttft =
        GetAvgTTFT(prompt_results.begin() + warmup_count, prompt_results.end());
    avg_2nd_latency = GetAvgNthTokenLatency(
        prompt_results.begin() + warmup_count, prompt_results.end());

    const auto sum_avg_token_lat = std::accumulate(
        prompt_results.begin() + warmup_count, prompt_results.end(),
        MillisecDuration{0.0}, [](MillisecDuration l, const auto& r) {
          return l + std::chrono::duration_cast<MillisecDuration>(
                         r.end_time - r.start_time) /
                         r.tokens.size();
        });
    avg_token_latency =
        sum_avg_token_lat / (prompt_results.size() - warmup_count);
  }

  PromptPerformanceResult(const PromptPerformanceResult& o) = default;
  PromptPerformanceResult(PromptPerformanceResult&& o) = default;

  std::string ToString() {
    std::stringstream ss;
    ss << "[ Runs: " << total_runs_count
       << ", Average input: " << average_input_tokens
       << ", Average generated: " << average_generated_tokens
       << ", AVG duration (ms) " << std::fixed << std::setprecision(3)
       << avg_duration.count() << ", AVG TTFT (ms) " << std::fixed
       << std::setprecision(3) << avg_ttft.count() << ", AVG latency (ms) "
       << std::fixed << std::setprecision(3) << avg_token_latency.count()
       << ", AVG 2nd latency (ms) " << std::fixed << std::setprecision(3)
       << avg_2nd_latency.count() << " ]";

    return ss.str();
  }

  // Return avg time to first token for given results
  static MillisecDuration GetAvgTTFT(ResultIter begin, ResultIter end) {
    MillisecDuration s{0.0};
    size_t cnt = 0;
    for (auto it = begin; it != end; ++it) {
      if (it->timestamps.size() > 0) {
        cnt++;
        s += std::chrono::duration_cast<MillisecDuration>(it->timestamps[0] -
                                                          it->start_time);
      }
    }
    return cnt != 0 ? MillisecDuration{s / cnt} : MillisecDuration{0.0};
  }

  // Return average token latency given offset
  // offset:
  //  0 - 2nd+ token latency
  //  1 - 3rd+ token latency, etc.
  MillisecDuration GetAvgNthTokenLatency(ResultIter begin, ResultIter end,
                                         size_t offset = 0) {
    MillisecDuration s{0.0};
    size_t cnt = 0;
    for (auto it = begin; it != end; ++it) {
      if (it->timestamps.size() > offset + 1) {
        cnt++;
        s += std::chrono::duration_cast<MillisecDuration>(
                 it->end_time - it->timestamps[offset]) /
             (it->timestamps.size() - offset - 1);
      }
    }
    return cnt != 0 ? MillisecDuration{s / cnt} : MillisecDuration{0.0};
  }

  bool has_empty_output = true;
  std::string category;
  size_t total_runs_count = 0;  // Including warmup
  double average_input_tokens = 0;
  double average_generated_tokens = 0;
  MillisecDuration avg_duration{0.0};
  MillisecDuration avg_ttft{0.0};
  MillisecDuration avg_token_latency{0.0};
  MillisecDuration avg_2nd_latency{0.0};
};

}  // namespace

namespace cil {
namespace infer {

int CountPromptsInFile(const std::string& file_path);

std::string ModelTypeToString(LLMExecutor::ModelType model_type) {
  using enum LLMExecutor::ModelType;
  switch (model_type) {
    case Llama2:
      return "Llama2";
    case Llama3:
      return "Llama3";
    case Phi3_5:
      return "Phi3.5";
    case Phi4:
      return "Phi4";
    default:
      return "Unknown";
  }
}

static log4cxx::LoggerPtr& GetLoggerByModelType(
    LLMExecutor::ModelType model_type) {
  using enum LLMExecutor::ModelType;
  switch (model_type) {
    case Llama2:
      return llama2_executor_logger;
    case Llama3:
      return llama3_executor_logger;
    case Phi3_5:
      return phi3_5_executor_logger;
    case Phi4:
      return phi4_executor_logger;
    default:
      throw std::runtime_error("Unknown model type: " +
                               std::to_string(static_cast<int>(model_type)));
  }
}

LLMExecutor::LLMExecutor(ModelType model_type, const std::string& model_path,
                         std::shared_ptr<ScenarioDataProvider> data_provider,
                         const std::string& library_path,
                         const std::string& ep_name,
                         const nlohmann::json& ep_config, const int iterations,
                         const int iterations_warmup,
                         const double inference_delay,
                         const bool skip_failed_prompts)
    : ExecutorBase(ep_name, ep_config, iterations, iterations_warmup,
                   inference_delay, library_path),
      model_type_(model_type),
      model_path_(model_path),
      input_paths_(data_provider->GetInputPaths()),
      data_provider_(data_provider),
      status_(Status::kReady),
      progress_(0),
      total_prompts_(0),
      current_prompt_(0),
      task_description_(ep_name + " for " + ModelTypeToString(model_type)),
      llm_executor_logger_(GetLoggerByModelType(model_type)),
      skip_failed_prompts_(skip_failed_prompts) {
  // init result
  benchmark_result_.scenario_name = ModelTypeToString(model_type);

  model_path_ = utils::NormalizePath(model_path_);

  // If the model path is a file, get the parent directory instead.
  if (!fs::is_regular_file(model_path_)) {
    benchmark_result_.error_message =
        "Model path is not a file: " + model_path_;
    status_ = Status::kFailed;
    return;
  }

  benchmark_result_.model_file_name = utils::GetFileNameFromPath(model_path_);

  for (const auto& path : data_provider_->GetAssetPaths())
    benchmark_result_.asset_file_names.push_back(
        utils::GetFileNameFromPath(path));
  for (const auto& path : input_paths_)
    benchmark_result_.data_file_names.push_back(
        utils::GetFileNameFromPath(path));
  expected_tokens_ = data_provider->GetExpectedTokens();

  benchmark_result_.execution_provider_name = ep_name;
  benchmark_result_.ep_configuration = ep_config;
  benchmark_result_.iterations = iterations;
  benchmark_result_.benchmark_success = false;
  benchmark_result_.duration = 0.0;
  benchmark_result_.results_verified = false;

  TfmStatus status;

  std::string tokenizer_parent_path =
      fs::path(data_provider_->GetLLMTokenizerPath()).parent_path().string();
  try {
    tokenizer_ = tfm::CreateTokenizer(tokenizer_parent_path, "", &status);
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(llm_executor_logger_,
                  "Failed to create tokenizer: " << e.what());
    benchmark_result_.error_message = "Failed to create tokenizer";

    status_ = Status::kFailed;
    return;
  }

  if (status.code() != 0) {
    LOG4CXX_ERROR(llm_executor_logger_,
                  "Failed to create tokenizer: " << status.message());
    benchmark_result_.error_message = "Failed to create tokenizer";

    status_ = Status::kFailed;
  }
}

bool LLMExecutor::Run() {
  if (status_ != Status::kReady) return false;

  start_time_ = std::chrono::high_resolution_clock::now();
  benchmark_result_.benchmark_start_time =
      utils::GetDateTimeString(start_time_);

  if (!fs::exists(model_path_) || !fs::is_regular_file(model_path_)) {
    benchmark_result_.error_message =
        "Model path does not exist or is not a file, path: " + model_path_;
  } else if (iterations_ <= 0) {
    benchmark_result_.error_message = "Iterations number should be > 0";
  } else if (iterations_warmup_ < 0) {
    benchmark_result_.error_message = "Iterations warmup number should be >= 0";
  } else if (input_paths_.empty()) {
    benchmark_result_.error_message = "Input paths can not be empty.";
  } else {
    for (const std::string& path : input_paths_) {
      if (!fs::exists(path) || !fs::is_regular_file(path)) {
        benchmark_result_.error_message =
            "Input path does not exist or is not a file, path: " + path;
      }
    }
  }

  benchmark_result_.iterations_warmup = iterations_warmup_;

  std::string input_file_schema_path =
      utils::NormalizePath(data_provider_->GetInputFileSchemaPath());

  if (!fs::exists(input_file_schema_path) ||
      !fs::is_regular_file(input_file_schema_path)) {
    benchmark_result_.error_message =
        "Input data schema path does not exist or is not a file, path: " +
        input_file_schema_path;
    status_ = Status::kFailed;
    return false;
  }

  // Verify input data and count total prompts
  int total_prompts = 0;
  for (const std::string& path : input_paths_) {
    // Verify existence of input path
    if (!std::filesystem::exists(path)) {
      benchmark_result_.error_message = "Input path does not exist: " + path;
      status_ = Status::kFailed;
      return false;
    }

    // Verify format of json
    std::ifstream input_file(path);
    nlohmann::json input_json;
    try {
      input_file >> input_json;
    } catch (const nlohmann::json::parse_error& e) {
      benchmark_result_.error_message =
          "Failed to parse input json: " + std::string(e.what());
      status_ = Status::kFailed;
      return false;
    }
    input_file.close();
    std::string error_string =
        cil::ValidateJSONSchema(input_file_schema_path, input_json);
    if (!error_string.empty()) {
      benchmark_result_.error_message =
          "Input file schema validation failed: " + path + ", " + error_string;
      status_ = Status::kFailed;
      return false;
    }

    total_prompts += CountPromptsInFile(path);
  }

  total_prompts_ = total_prompts;

  if (!benchmark_result_.error_message.empty()) {
    LOG4CXX_ERROR(llm_executor_logger_,
                  "\n" << benchmark_result_.error_message);
    status_ = Status::kFailed;
    return false;
  }

  status_ = Status::kInitializing;
  progress_ = 0;

  EP ep = NameToEP(ep_name_);
  if (ep == EP::kUnknown) {
    benchmark_result_.error_message = "Unknown execution provider: " + ep_name_;
    status_ = Status::kFailed;
  } else {
    RunLLMTask(ep, ep_settings_);
  }

  if (status_ == Status::kCanceled)
    benchmark_result_.error_message = "Run was cancelled";

  if (!benchmark_result_.error_message.empty()) {
    LOG4CXX_ERROR(llm_executor_logger_,
                  "\n" << benchmark_result_.error_message);
  }

  return true;
}

BenchmarkResult LLMExecutor::GetBenchmarkResult() const {
  return benchmark_result_;
}

void LLMExecutor::Cancel() {
  // can not be canceled if already completed
  if (status_ == Status::kCompleted) return;
  status_ = Status::kCanceled;
}

ProgressableTask::Status LLMExecutor::GetStatus() const { return status_; }

int LLMExecutor::GetProgress() const { return progress_; }

int LLMExecutor::GetTotalSteps() const { return total_prompts_; }

int LLMExecutor::GetCurrentStep() const { return current_prompt_; }

std::chrono::high_resolution_clock::time_point LLMExecutor::GetStartTime()
    const {
  return start_time_;
}

std::string LLMExecutor::GetDescription() {
  std::lock_guard<std::mutex> lock(task_description_mutex_);
  return task_description_;
}

int CountPromptsInFile(const std::string& file_path) {
  std::ifstream input_file(file_path);
  nlohmann::json input_json;
  input_file >> input_json;
  input_file.close();
  return input_json["prompts"].size();
}

void LLMExecutor::RunLLMTask(EP ep, const nlohmann::json& settings) {
  std::string model_file_path = fs::path(model_path_).string();

  std::string model_type;
  switch (model_type_) {
    case ModelType::Llama2:
      model_type = "Llama2";
      break;
    case ModelType::Llama3:
      model_type = "Llama3";
      break;
    case ModelType::Phi3_5:
      model_type = "Phi3.5";
      break;
    case ModelType::Phi4:
      model_type = "Phi4";
      break;
    default:
      model_type = "unknown";
  }

  std::unique_ptr<LLMInference> inference;
  try {
    inference = std::make_unique<LLMInference>(
        model_type, model_file_path, deps_dir_, ep, settings, library_path_);
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
    task_description_ =
        ep_name_ + " on " + inference->GetDeviceType() + " for " + model_type;
  }

  if (status_ == Status::kCanceled) {
    return;
  }

  status_ = Status::kRunning;

  int benchmark_items_count = 0;

  // Add warmup interations to skip them
  int sessions_count = (iterations_ + iterations_warmup_) * total_prompts_;
  int current_session_index = 0;

  int all_correct_predictions_count = 0;
  int all_predictions_count = 0;

  size_t all_prompt_index = 0;

  std::string last_model_config;
  bool model_initialized = false;
  bool low_tokens_count = false;

  LOG4CXX_INFO(
      llm_executor_logger_,
      "Running " << ep_name_ << " on " << inference->GetDeviceType() << " for "
                 << model_type
                 << " with " << iterations_ << " iterations."
                 << " The first iteration will be used to warm up the log.");

  std::unordered_map<std::string, std::vector<PromptPerformanceResult>>
      prompt_perf_results;

  std::vector<std::vector<size_t>> skipped_prompts;
  int32_t skipped_prompts_count = 0;
  for (const std::string& input_path : input_paths_) {
    if (status_ == Status::kCanceled) return;

    // Read input json to get input prompts using nlohmann::json
    std::vector<std::string> input_prompts;
    std::string category = "Unknown";
    std::ifstream input_file(input_path);
    nlohmann::json input_json;
    input_file >> input_json;
    input_file.close();
    for (const auto& prompt : input_json["prompts"]) {
      input_prompts.push_back(prompt);
    }

    if (input_json.contains("category")) {
      category = input_json["category"];
    }

    // Load model configs
    std::string model_config = input_json["model_config"].dump();

    if (!model_initialized) {
      inference->Init(model_config);
      last_model_config = model_config;
    } else if (model_config != last_model_config) {
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

    std::vector<size_t> skipped_prompts_for_input;
    size_t prompt_index = 0;
    for (size_t i = 0; i < input_prompts.size(); ++i) {
      const auto& prompt = input_prompts[i];
      LOG4CXX_DEBUG(llm_executor_logger_, "Prompt: " + prompt);

      // Tokenize input text
      std::vector<std::string_view> input_texts = {prompt};
      std::vector<std::vector<uint32_t>> token_ids;
      const auto status = tokenizer_->Tokenize(input_texts, token_ids);
      if (!status.ok() || token_ids[0].size() == 0) {
        status_ = Status::kFailed;
        benchmark_result_.error_message = std::string(status.error_message());
        inference->Deinit();
        return;
      }

      std::vector<LLMInference::Result> prompt_results;
      prompt_index = all_prompt_index;

      // Run multiple inferences to average results
      bool is_success = true;

      auto check_error = [&]() {
        if (const auto& infer_error_message = inference->GetErrorMessage();
            !infer_error_message.empty()) {
          status_ = Status::kFailed;
          benchmark_result_.error_message = inference->GetErrorMessage(false);
          benchmark_result_.ep_error_messages = inference->GetEPErrorMessages();
          is_success = false;
          return true;
        }
        return false;
      };

      for (int iter = 0; iter < iterations_ + iterations_warmup_; ++iter,
               ++current_session_index,
               progress_ = current_session_index * 100 / sessions_count) {
        if (status_ == Status::kCanceled) {
          LOG4CXX_DEBUG(llm_executor_logger_, "Canceled");
          inference->Deinit();
          return;
        }

        // Run inference
        inference->Prepare();

        if (check_error()) {
          break;
        }

        // Delay before running the next iteration/prompt
        std::this_thread::sleep_for(inference_delay_);
        auto results = inference->Run(token_ids[0]);
        results.category = category;
        prompt_results.push_back(results);

        // Even if there was an error, let's call Reset,
        // to ensure proper cleanup
        bool infer_error = check_error();

        inference->Reset();

        if (infer_error || check_error()) {
          break;
        }

        // Detokenize output ids
        tfm::span<tfmTokenId_t const> output_ids_span(
            prompt_results.back().tokens);
        std::vector<tfm::span<tfmTokenId_t const>> output_ids_spans = {
            output_ids_span};
        std::vector<std::string> decoded_output;
        TfmStatus decode_status =
            tokenizer_->Detokenize(output_ids_spans, decoded_output);

        if (decode_status.code() != 0) {
          LOG4CXX_ERROR(llm_executor_logger_, "Failed to detokenize output: "
                                                  << decode_status.message());
          status_ = Status::kFailed;
          benchmark_result_.error_message = decode_status.message();
          inference->Deinit();
          return;
        }

        if (iter == 0) {
          LOG4CXX_DEBUG(llm_executor_logger_, "Input: " + prompt);
#if DEBUG_OUTPUT_IDS || 0
          std::string expected_ids_str;
          std::vector<uint32_t>& expected_output_ids =
              expected_tokens_[prompt_index];
          for (const auto id : expected_output_ids) {
            expected_ids_str += std::to_string(id) + ", ";
          }
          expected_ids_str =
              expected_ids_str.substr(0, expected_ids_str.size() - 2) + " (" +
              std::to_string(expected_output_ids.size()) + " tokens)";

          std::string output_ids_str;
          for (const auto id : output_ids_span) {
            output_ids_str += std::to_string(id) + ", ";
          }
          output_ids_str = output_ids_str.substr(0, output_ids_str.size() - 2) +
                           " (" + std::to_string(output_ids_span.size()) +
                           " tokens)";
          LOG4CXX_DEBUG(llm_executor_logger_,
                        "  Output ids: " + output_ids_str);
          LOG4CXX_DEBUG(llm_executor_logger_,
                        "Expected ids: " + expected_ids_str);
#endif
          LOG4CXX_DEBUG(llm_executor_logger_, "Output: " + decoded_output[0]);
          benchmark_result_.output.emplace_back(decoded_output[0]);
        }
      }  // iters

      // Count correct predictions
      LOG4CXX_INFO(llm_executor_logger_,
                   "Validating prompt results " + std::to_string(prompt_index) +
                       "/" + std::to_string(expected_tokens_.size()));

      if (expected_tokens_.size() <= prompt_index) {
        LOG4CXX_WARN(llm_executor_logger_, "Prompt results are not provided.");
      } else {
        std::vector<uint32_t>& expected_output_ids =
            expected_tokens_[prompt_index];
        size_t correct_predictions_count =
            std::count_if(prompt_results.begin(), prompt_results.end(),
                          [&expected_output_ids](auto& r) {
                            return r.tokens == expected_output_ids;
                          });

        LOG4CXX_DEBUG(
            llm_executor_logger_,
            "Correct results: " << correct_predictions_count << "/"
                                << prompt_results.size() << " - Verified on "
                                << expected_output_ids.size() << " tokens");

        all_correct_predictions_count += correct_predictions_count;
      }
      ++prompt_index;
      all_predictions_count += prompt_results.size();

      bool empty_tokens =
          std::any_of(prompt_results.begin(), prompt_results.end(),
                      [](const auto& prompt_result) {
                        return prompt_result.tokens.empty();
                      });

      if (!empty_tokens) {
        low_tokens_count =
            low_tokens_count ||
            std::any_of(prompt_results.begin(), prompt_results.end(),
                        [](const auto& prompt_result) {
                          return prompt_result.tokens.size() < 2;
                        });
      }

      // Update benchmark time and progress
      if (!is_success || empty_tokens) {
        if (skip_failed_prompts_) {
          skipped_prompts_for_input.push_back(i);
          skipped_prompts_count++;
          inference->ClearErrorMessage();
          inference->Deinit();
          inference->Init(model_config);

          continue;
        }
        else {
          status_ = Status::kFailed;
          if (benchmark_result_.error_message.empty()) {
            benchmark_result_.error_message =
                "There is empty output in the result.";
          }
          inference->Deinit();
          return;
        }
      }

      prompt_perf_results[category].emplace_back(prompt_results,
                                                 iterations_warmup_);
      LOG4CXX_DEBUG(llm_executor_logger_,
                    "Prompt timing results: " +
                        prompt_perf_results[category].back().ToString());
    }  // prompts

    skipped_prompts.push_back(skipped_prompts_for_input);

    all_prompt_index = prompt_index;
    current_prompt_ = prompt_index;
  }  // input path

  if (model_initialized) {
    inference->Deinit();
  }

  if (status_ == Status::kRunning) {
    status_ = Status::kCompleted;
  }

  /// Calculate overall benchmark metrics
  benchmark_result_.is_llm_benchmark = true;
  benchmark_result_.benchmark_success = status_ == Status::kCompleted;
  benchmark_result_.device_type = inference->GetDeviceType();

  constexpr double kMsToSecRatio = 1e3;

  CategoryPerformanceResult per_category_perf_results{};

  PromptPerformanceResult::MillisecDuration avg_duration_sum{0.0};
  size_t avg_duration_cnt{0};
  for (const auto& [category, perf_results] : prompt_perf_results) {
    avg_duration_sum += std::accumulate(
        perf_results.begin(), perf_results.end(),
        PromptPerformanceResult::MillisecDuration{0.0},
        [](auto a, const auto& r) { return a + r.avg_duration; });
    avg_duration_cnt += perf_results.size();

    per_category_perf_results[category].average_generated_tokens =
        arithmean<double>(
            perf_results.begin(), perf_results.end(),
            [](const auto& r) { return r.average_generated_tokens; });

    per_category_perf_results[category].average_input_tokens =
        arithmean<double>(perf_results.begin(), perf_results.end(),
                          [](const auto& r) { return r.average_input_tokens; });

    if (!low_tokens_count) {
      // 2nd+ token generation rate (token/sec)
      per_category_perf_results[category].token_generation_rate =
          arithmean<double>(perf_results.begin(), perf_results.end(),
                            [](const auto& r) {
                              return kMsToSecRatio / r.avg_2nd_latency.count();
                            });
    }

    // Average the time to first token duration in sec
    per_category_perf_results[category].time_to_first_token_duration =
        arithmean<double>(
            perf_results.begin(), perf_results.end(),
            [](const auto& r) { return r.avg_ttft.count() / kMsToSecRatio; });
  }

  if (skipped_prompts_count == 0) {
    const auto& requiredCategories = BenchmarkResult::kLLMRequiredCategories;

    bool haves_all_required_categories =
        per_category_perf_results.size() == requiredCategories.size();

    if (haves_all_required_categories) {
      if ((ep == EP::kIHVWindowsML) && (utils::StringToLowerCase(settings.value("device_ep", "null")) != "openvino")) {
        haves_all_required_categories = false;
        LOG4CXX_WARN(llm_executor_logger_,
                       "IHV WindowsML without OpenVINO device_ep. Overall "
                       "performance results will not be calculated.");
      } else {
        for (const auto& category : per_category_perf_results) {
          if (requiredCategories.find(category.first) ==
              requiredCategories.end()) {
            haves_all_required_categories = false;
            LOG4CXX_WARN(llm_executor_logger_,
                         "Some required categories are missing. Overall "
                         "performance results will not be calculated.");
            break;
          }
        }
      }
    } else if (per_category_perf_results.size() > requiredCategories.size()) {
      LOG4CXX_WARN(llm_executor_logger_,
                   "Some categories are not required. Overall performance "
                   "results will not be calculated.");
    }

    if (haves_all_required_categories) {
      PerformanceResult overall_performance_results{};

      overall_performance_results.average_generated_tokens = arithmean<double>(
          per_category_perf_results.begin(), per_category_perf_results.end(),
          [](const auto& r) { return r.second.average_generated_tokens; });
      overall_performance_results.average_input_tokens = arithmean<double>(
          per_category_perf_results.begin(), per_category_perf_results.end(),
          [](const auto& r) { return r.second.average_input_tokens; });
      overall_performance_results.time_to_first_token_duration =
          geomean<double>(per_category_perf_results.begin(),
                          per_category_perf_results.end(), [](const auto& r) {
                            return r.second.time_to_first_token_duration;
                          });
      overall_performance_results.token_generation_rate = geomean<double>(
          per_category_perf_results.begin(), per_category_perf_results.end(),
          [](const auto& r) { return r.second.token_generation_rate; });

      if (!per_category_perf_results
               .try_emplace(BenchmarkResult::kLLMOverallCategory,
                            overall_performance_results)
               .second) {
        LOG4CXX_ERROR(llm_executor_logger_,
                      "'Overall' category already present in the list. Prompts "
                      "shall not have it.");
        status_ = Status::kFailed;
      }
    }
  } else {
    LOG4CXX_WARN(llm_executor_logger_,
                 "Some prompts were skipped due to empty output. Overall "
                 "performance results will not be calculated.");
  }

  benchmark_result_.duration =
      (avg_duration_sum / avg_duration_cnt).count() / kMsToSecRatio;
  benchmark_result_.performance_results = per_category_perf_results;
  benchmark_result_.results_verified =
      all_correct_predictions_count == all_predictions_count;

  benchmark_result_.skipped_prompts = std::move(skipped_prompts);
}

}  // namespace infer
}  // namespace cil
