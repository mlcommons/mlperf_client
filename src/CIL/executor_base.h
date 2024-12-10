/**
 * @file executor_base.h
 * @brief The ExecutorBase class is the base class for all the executors.
 *
 * The ExecutorBase class is the base class for all the executors. It provides
 * the interface for the executors to implement the method to get the benchmark
 * result. and to be able to run the executor as a task in the TaskScheduler.
 */

#ifndef EXECUTOR_BASE_H_
#define EXECUTOR_BASE_H_

#include <filesystem>

#include "benchmark_result.h"
#include "progressable_task.h"

namespace cil {
namespace infer {

/**
 * @class ExecutorBase
 * @brief The ExecutorBase class is the base class for all the executors.
 *
 * The ExecutorBase class is the base class for all the executors. It provides
 * the interface for the executors to implement the method to get the benchmark
 * result. and to be able to run the executor as a progressable task in the
 * TaskScheduler.
 */
class ExecutorBase : public ProgressableTask {
 public:
  inline ExecutorBase(const std::string& ep_name,
                      const nlohmann::json& ep_config, const int iterations,
                      const int iterations_warmup, const double inference_delay,
                      const std::string& library_path)
      : ProgressableTask(),
        library_path_(library_path),
        ep_name_(ep_name),
        ep_settings_(ep_config),
        iterations_(iterations),
        iterations_warmup_(iterations_warmup),
        inference_delay_(std::chrono::milliseconds(
            static_cast<long long>(inference_delay * 1000))) {
    deps_dir_ = std::filesystem::path(library_path_).parent_path().string();
  }
  virtual ~ExecutorBase(){};
  /**
   * @brief A pure virtual function to get the benchmark result.
   *
   * This method should be implemented by the derived classes to get the
   * benchmark result.
   *
   */
  virtual BenchmarkResult GetBenchmarkResult() const = 0;

 protected:
  const std::string library_path_;
  std::string deps_dir_;

  const std::string ep_name_;
  const nlohmann::json ep_settings_;
  const int iterations_;
  const int iterations_warmup_;
  const std::chrono::milliseconds inference_delay_;
};

}  // namespace infer
}  // namespace cil

#endif  // EXECUTOR_BASE_H_
