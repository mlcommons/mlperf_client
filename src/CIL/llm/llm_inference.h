#ifndef LLAMA2_INFERENCE_H_
#define LLAMA2_INFERENCE_H_

#include <nlohmann/json.hpp>
#include <ratio>
#include <tuple>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "base_inference.h"
#include "../system_info_provider.h"

#define HW_MONITORING 1

namespace cil::infer {

class LLMInference : public BaseInference {
 public:
  using Clock = std::chrono::high_resolution_clock;
  using Token = uint32_t;
  using Timestamp = Clock::time_point;
  using MillisecDuration = std::chrono::duration<double, std::milli>;
  using HWInfoRecord = std::tuple<cil::SystemInfoProvider::MemoryInfo, Timestamp>;

  struct Result {
    Result(size_t input_tokens_count, const std::vector<Token>& tokens,
           const std::vector<Timestamp>& timestamps, const std::vector<HWInfoRecord>& hw_info_records,
           Timestamp start_time, Timestamp end_time)
        : input_tokens_count(input_tokens_count),
          tokens(tokens),
          timestamps(timestamps),
          hw_info_records(hw_info_records),
          start_time(start_time),
          end_time(end_time) {}

    Result() = default;
    Result(const Result& o) = default;
    Result(Result&& o) = default;

    size_t input_tokens_count;          // Number of input tokens
    std::vector<Token> tokens;          // Output tokens
    std::vector<Timestamp> timestamps;  // Timestamp when token was generated
    std::vector<HWInfoRecord>
        hw_info_records;                // System memory info
    Timestamp start_time;               // Inference call start time
    Timestamp end_time;                 // Inference call end time
    std::string category;               // Prompt category
  };

  LLMInference(const std::string& model_type, const std::string& model_path,
               const std::string& deps_dir,
                  EP ep, const nlohmann::json& ep_settings,
                  const std::string& library_path);
  ~LLMInference();

  bool IsValid() const;

  // Load the model and initialize the pipeline
  void Init(const std::string& config);
  // Cleanup inference model
  void Deinit();

  // Do any preparation before running workload (allocate temp buffers, etc)
  void Prepare();
  // Reset state after each execution (flush IO, output stats, etc)
  void Reset();

  // Run model inference
  // Result must be valid until reset() call
  Result Run(const std::vector<Token>& input_data);

  /**
   * @brief Clears the error message.
   *
   * This method clears the error message
   */
  void ClearErrorMessage();

 private:
  // Callback method called by IHV inference implementation for each token
  static void TokenCallback(void* object, Token token);

  std::vector<Token> tokens_;
  std::vector<Timestamp> timestamps_;
  const std::string model_type_;

  std::vector<HWInfoRecord> hw_info_records_;
  void LogHardwareInfo();

  std::mutex hw_info_mutex_;

#ifdef HW_MONITORING
  // Hardware monitoring thread to collect system utilization data
  std::thread hw_monitor_thread_;
  std::atomic<bool> hw_monitor_collect_{false};
  std::atomic<bool> hw_monitor_stop_{false};
#endif
};

}  // namespace cil::infer

#endif  // !LLAMA2_INFERENCE_H_
