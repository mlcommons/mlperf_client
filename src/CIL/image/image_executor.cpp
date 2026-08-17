#include "image_executor.h"

#include <log4cxx/logger.h>

#include <filesystem>
#include <fstream>

#include "executor_logger.h"
#include "json_schema.h"
#include "prompt_file_reader.h"
#include "scenario_data_provider.h"
#include "tools/imageio.h"
#include "utils.h"

namespace fs = std::filesystem;

namespace cil {
namespace infer {

ImageExecutor::~ImageExecutor() = default;

ImageExecutor::ImageExecutor(
    const std::string& scenario_name, const std::string& model_base_name,
    const std::string& display_name, const std::string& model_path,
    std::shared_ptr<ScenarioDataProvider> data_provider,
    const std::string& library_path, const std::string& ep_name,
    const nlohmann::json& ep_config, int iterations, int iterations_warmup,
    double inference_delay, bool skip_failed_prompts)
    : ExecutorBase(ep_name, ep_config, iterations, iterations_warmup,
                   inference_delay, library_path),
      model_path_(model_path),
      input_paths_(data_provider->GetInputPaths()),
      data_provider_(data_provider),
      task_description_(ep_name + " for " + display_name),
      scenario_name_(scenario_name),
      model_base_name_(model_base_name),
      display_name_(display_name),
      executor_logger_(ExecutorLogger(scenario_name, model_base_name).Get()),
      skip_failed_required_prompts_(skip_failed_prompts) {
  std::string imageio_error;
  image_io_ = std::make_unique<tools::ImageIO>(imageio_error);
  if (!imageio_error.empty()) {
    LOG4CXX_ERROR(executor_logger_, "imageio: " << imageio_error);
  }
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

  for (const auto& path : input_paths_)
    benchmark_result_.data_file_names.push_back(
        utils::GetFileNameFromPath(path));

  benchmark_result_.execution_provider_name = ep_name;
  benchmark_result_.ep_configuration = ep_config;
  benchmark_result_.iterations = iterations;
  benchmark_result_.benchmark_success = false;
  benchmark_result_.duration = 0.0;

  if (const std::string canonical =
          ExecutorLogger::ResolveCanonicalScenarioName(scenario_name);
      scenario_name == canonical) {
    const std::string& subfolder =
        model_base_name.empty() ? display_name : model_base_name;
    output_dir_ =
        utils::GetAppDefaultDataPath() / "Output" / canonical / subfolder;
  } else {
    std::string alias = utils::StringReplaceChar(scenario_name, '.', '_');
    output_dir_ = utils::GetAppDefaultDataPath() / "Output" / alias;
  }
  std::error_code ec;
  fs::create_directories(output_dir_, ec);
}

bool ImageExecutor::Run() {
  if (status_ != Status::kReady) return false;

  start_time_ = std::chrono::high_resolution_clock::now();
  benchmark_result_.benchmark_start_time =
      utils::GetDateTimeString(start_time_);

  if (iterations_ <= 0) {
    benchmark_result_.error_message = "Iterations number should be > 0";
  } else if (iterations_warmup_ < 0) {
    benchmark_result_.error_message = "Iterations warmup number should be >= 0";
  } else if (input_paths_.empty()) {
    benchmark_result_.error_message = "Input paths can not be empty.";
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
    if (!fs::exists(path)) {
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

BenchmarkResult ImageExecutor::GetBenchmarkResult() const {
  return benchmark_result_;
}

void ImageExecutor::Cancel() {
  if (status_ == Status::kCompleted) return;
  status_ = Status::kCanceled;
}

ProgressableTask::Status ImageExecutor::GetStatus() const { return status_; }

int ImageExecutor::GetProgress() const { return progress_; }

int ImageExecutor::GetTotalSteps() const { return total_prompts_; }

int ImageExecutor::GetCurrentStep() const { return current_prompt_; }

std::chrono::high_resolution_clock::time_point ImageExecutor::GetStartTime()
    const {
  return start_time_;
}

std::string ImageExecutor::GetDescription() {
  std::lock_guard<std::mutex> lock(task_description_mutex_);
  return task_description_;
}

bool ImageExecutor::SaveImageOrFail(const std::string& filename,
                                    const uint8_t* pixel_data, uint32_t width,
                                    uint32_t height) {
  if (!image_io_) {
    LOG4CXX_ERROR(executor_logger_, "imageio not loaded");
    return false;
  }
  const fs::path out_path = output_dir_ / filename;
  if (image_io_->SavePNG(out_path.string(), pixel_data, width, height)) {
    benchmark_result_.output_image_paths.push_back(out_path.string());
    LOG4CXX_INFO(executor_logger_, "Saved image: " + out_path.string());
    return true;
  }
  LOG4CXX_WARN(executor_logger_, "Failed to save image: " + out_path.string());
  return false;
}

}  // namespace infer
}  // namespace cil
