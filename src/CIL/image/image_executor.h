/**
 * @file image_executor.h
 *
 * @brief Abstract base class for image-generation executors (txt2img, ...).
 *
 * ImageExecutor owns all the plumbing common to any executor running an
 * image-generation model: path validation, progress/status tracking,
 * per-model logger, output directory management, PNG save helper, and the
 * BenchmarkResult.  Concrete scenarios implement RunScenario() with their
 * own inference loop.
 */
#ifndef IMAGE_EXECUTOR_H_
#define IMAGE_EXECUTOR_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "execution_provider.h"
#include "executor_base.h"

namespace log4cxx {
class Logger;
using LoggerPtr = std::shared_ptr<Logger>;
}  // namespace log4cxx

namespace cil {

namespace tools {
class ImageIO;
}  // namespace tools

class ScenarioDataProvider;

namespace infer {

class ImageExecutor : public ExecutorBase {
 public:
  ImageExecutor(const std::string& scenario_name,
                const std::string& model_base_name,
                const std::string& display_name, const std::string& model_path,
                std::shared_ptr<ScenarioDataProvider> data_provider,
                const std::string& library_path, const std::string& ep_name,
                const nlohmann::json& ep_config, int iterations,
                int iterations_warmup, double inference_delay,
                bool skip_failed_prompts);

  ~ImageExecutor() override;

  BenchmarkResult GetBenchmarkResult() const override;
  bool Run() override;
  void Cancel() override;
  Status GetStatus() const override;
  int GetProgress() const override;
  int GetTotalSteps() const override;
  int GetCurrentStep() const override;
  std::chrono::high_resolution_clock::time_point GetStartTime() const override;
  std::string GetDescription() override;

 protected:
  virtual void RunScenario(EP ep, const nlohmann::json& settings) = 0;

  bool SaveImageOrFail(const std::string& filename, const uint8_t* pixel_data,
                       uint32_t width, uint32_t height);

  std::string model_path_;
  std::vector<std::string> input_paths_;

  std::shared_ptr<ScenarioDataProvider> data_provider_;

  std::atomic<Status> status_{Status::kReady};
  std::atomic<int> progress_{0};
  std::atomic<int> total_prompts_{0};
  std::atomic<int> current_prompt_{0};
  std::atomic<std::chrono::high_resolution_clock::time_point> start_time_;
  std::string task_description_;
  std::mutex task_description_mutex_;
  BenchmarkResult benchmark_result_;

  const std::string scenario_name_;
  const std::string model_base_name_;
  const std::string display_name_;

  log4cxx::LoggerPtr executor_logger_;

  const bool skip_failed_required_prompts_;

  std::filesystem::path output_dir_;
  std::unique_ptr<tools::ImageIO> image_io_;
};

}  // namespace infer
}  // namespace cil

#endif  // IMAGE_EXECUTOR_H_
