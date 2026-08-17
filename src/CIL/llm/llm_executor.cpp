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
#include "executor_logger.h"
#include "json_schema.h"
#include "llm_inference.h"
#include "prompt_file_reader.h"
#include "scenario_data_provider.h"
#include "tools/tokenizer.h"

namespace fs = std::filesystem;

namespace cil {
namespace infer {

namespace {
nlohmann::json AugmentEpConfigWithAgenticFlag(const nlohmann::json& ep_config,
                                              bool is_agentic) {
  nlohmann::json augmented = ep_config;
  augmented["IsAgentic"] = is_agentic;
  return augmented;
}
}  // namespace

LLMExecutor::LLMExecutor(
    const std::string& scenario_name, const std::string& model_base_name,
    const std::string& display_name, const std::string& model_path,
    std::shared_ptr<ScenarioDataProvider> data_provider,
    const std::string& library_path, const std::string& ep_name,
    const nlohmann::json& ep_config, const int iterations,
    const int iterations_warmup, const double inference_delay,
    const bool skip_failed_prompts, const bool is_agentic,
    const bool tools_execution)
    : ExecutorBase(
          ep_name, AugmentEpConfigWithAgenticFlag(ep_config, is_agentic),
          iterations, iterations_warmup, inference_delay, library_path),
      model_path_(model_path),
      input_paths_(data_provider->GetInputPaths()),
      data_provider_(data_provider),
      task_description_(ep_name + " for " + display_name),
      scenario_name_(scenario_name),
      model_base_name_(model_base_name),
      display_name_(display_name),
      executor_logger_(ExecutorLogger(scenario_name, model_base_name).Get()),
      skip_failed_required_prompts_(skip_failed_prompts),
      is_agentic_(is_agentic),
      tools_execution_(tools_execution) {
  benchmark_result_.scenario_name = scenario_name;
  benchmark_result_.model_base_name = model_base_name;

  model_path_ = utils::NormalizePath(model_path_);

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

  std::string error;

  std::string tokenizer_parent_path =
      fs::path(data_provider_->GetLLMTokenizerPath()).parent_path().string();
  try {
    tokenizer_ =
        std::make_unique<cil::tools::Tokenizer>(tokenizer_parent_path, error);
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(executor_logger_, "Failed to create tokenizer: " << e.what());
    benchmark_result_.error_message = "Failed to create tokenizer";

    status_ = Status::kFailed;
    return;
  }

  if (!error.empty()) {
    LOG4CXX_ERROR(executor_logger_, "Failed to create tokenizer: " << error);
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

  int total_prompts = 0;
  for (const std::string& path : input_paths_) {
    if (!std::filesystem::exists(path)) {
      benchmark_result_.error_message = "Input path does not exist: " + path;
      status_ = Status::kFailed;
      return false;
    }

    std::ifstream input_file(path);
    nlohmann::json input_json;
    try {
      input_file >> input_json;
    } catch (const nlohmann::json::parse_error& e) {
      benchmark_result_.error_message = "Failed to parse input json " + path +
                                        ", error: " + std::string(e.what());
      status_ = Status::kFailed;
      return false;
    }
    input_file.close();
    if (std::string error_string =
            cil::ValidateJSONSchema(input_file_schema_path, input_json);
        !error_string.empty()) {
      benchmark_result_.error_message =
          "Input file schema validation failed: " + path + ", " + error_string;
      status_ = Status::kFailed;
      return false;
    }

    total_prompts += PromptFileReader::CountPromptsInFile(path);
  }

  total_prompts_ = total_prompts;

  if (!benchmark_result_.error_message.empty()) {
    LOG4CXX_ERROR(executor_logger_, "\n" << benchmark_result_.error_message);
    status_ = Status::kFailed;
    return false;
  }

  status_ = Status::kInitializing;
  progress_ = 0;

  if (EP ep = NameToEP(ep_name_); ep == EP::kUnknown) {
    benchmark_result_.error_message = "Unknown execution provider: " + ep_name_;
    status_ = Status::kFailed;
  } else {
    RunScenario(ep, ep_settings_);
  }

  if (status_ == Status::kCanceled)
    benchmark_result_.error_message = "Run was cancelled";

  if (!benchmark_result_.error_message.empty()) {
    LOG4CXX_ERROR(executor_logger_, "\n" << benchmark_result_.error_message);
  }

  return true;
}

BenchmarkResult LLMExecutor::GetBenchmarkResult() const {
  return benchmark_result_;
}

void LLMExecutor::Cancel() {
  if (status_ == Status::kCompleted) return;
  status_ = Status::kCanceled;
}

ProgressableTask::Status LLMExecutor::GetStatus() const { return status_; }

int LLMExecutor::GetProgress() const { return progress_; }

int LLMExecutor::GetTotalSteps() const {
  return is_agentic_ ? static_cast<int>(input_paths_.size())
                     : total_prompts_.load();
}

int LLMExecutor::GetCurrentStep() const { return current_prompt_; }

std::chrono::high_resolution_clock::time_point LLMExecutor::GetStartTime()
    const {
  return start_time_;
}

std::string LLMExecutor::GetDescription() {
  std::lock_guard<std::mutex> lock(task_description_mutex_);
  return task_description_;
}

bool LLMExecutor::TokenizeOrFail(std::string_view text,
                                 std::vector<uint32_t>& out_tokens,
                                 bool add_special_tokens) {
  std::vector<std::string_view> input_texts = {text};
  auto result = tokenizer_->Encode(input_texts, add_special_tokens);
  if (!result) {
    LOG4CXX_ERROR(executor_logger_,
                  "Failed to tokenize input: " << result.error());
    benchmark_result_.error_message = result.error();
    status_ = Status::kFailed;
    return false;
  }
  if (result.value().empty() || result.value()[0].empty()) {
    std::string msg = "Tokenizer produced no tokens";
    LOG4CXX_ERROR(executor_logger_, "Failed to tokenize input: " << msg);
    benchmark_result_.error_message = std::move(msg);
    status_ = Status::kFailed;
    return false;
  }
  out_tokens = std::move(result.value()[0]);
  return true;
}

bool LLMExecutor::DetokenizeOrFail(std::span<const uint32_t> tokens,
                                   std::string& out_text) {
  std::vector<std::span<const uint32_t>> token_spans = {tokens};
  auto result = tokenizer_->Decode(token_spans);
  if (!result) {
    LOG4CXX_ERROR(executor_logger_,
                  "Failed to detokenize output: " << result.error());
    benchmark_result_.error_message = result.error();
    status_ = Status::kFailed;
    return false;
  }
  if (result.value().empty()) {
    std::string msg = "Detokenizer produced no output";
    LOG4CXX_ERROR(executor_logger_, "Failed to detokenize output: " << msg);
    benchmark_result_.error_message = std::move(msg);
    status_ = Status::kFailed;
    return false;
  }
  out_text = std::move(result.value()[0]);
  return true;
}

}  // namespace infer
}  // namespace cil
