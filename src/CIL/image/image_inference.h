#ifndef IMAGE_INFERENCE_H_
#define IMAGE_INFERENCE_H_

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "IHV/IHV.h"
#include "base_inference.h"

namespace cil::infer {

class ImageInference : public BaseInference {
 public:
  using Clock = std::chrono::high_resolution_clock;
  using Timestamp = Clock::time_point;

  struct Result {
    std::vector<uint8_t> pixel_data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t num_generated_images = 0;
    Timestamp generation_start;
    Timestamp generation_end;
    Timestamp infer_start;
    Timestamp infer_end;
    std::vector<Timestamp> step_timestamps;
    std::unordered_map<std::string, std::vector<double>> submodule_durations_ms;
    std::string category;
  };

  ImageInference(const std::string& scenario_name,
                 const std::string& model_base_name,
                 const std::string& model_path, const std::string& deps_dir,
                 EP ep, const nlohmann::json& ep_settings,
                 const std::string& library_path,
                 const std::string& logger_name = "");
  ~ImageInference();

  bool IsValid() const;

  void Init(const std::string& config);
  void Deinit();
  void Prepare();
  void Reset();

  Result Run(const std::string& prompt_json, uint32_t width, uint32_t height);

  double GetLastInitSeconds() const { return last_init_seconds_; }

  void ClearErrorMessage();

 private:
  static void ImageStepCallback(void* object,
                                const API_IHV_ImageStep_Event_t* event);

  Timestamp generation_start_;
  Timestamp generation_end_;
  std::vector<Timestamp> step_timestamps_;
  std::unordered_map<std::string, std::vector<double>> submodule_durations_ms_;
  double last_init_seconds_ = 0.0;
};

}  // namespace cil::infer

#endif  // IMAGE_INFERENCE_H_
