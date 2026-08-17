#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "../../IHV.h"
#include "../base_inference.h"

namespace cil {
namespace IHV {
namespace infer {

class ImageInference : public BaseInference {
 public:
  ImageInference(const std::string& model_path, const std::string& model_name,
                 const NativeOpenVINOExecutionProviderSettings& ep_settings,
                 cil::Logger logger, const std::string& deps_dir);
  ~ImageInference();

  void Init(const nlohmann::json& model_config,
            std::optional<API_IHV_DeviceID_t> device_id);

  void Prepare() override;

  void Run(const std::string& prompt_json,
           std::function<void(unsigned, unsigned)> step_callback);

  void Reset() override;

  void Deinit() override;

  const uint8_t* GetOutputData() const;
  size_t GetOutputSize() const;
  uint32_t GetWidth() const;
  uint32_t GetHeight() const;
  uint32_t GetNumImages() const;

 private:
  uint32_t width_ = 1024;
  uint32_t height_ = 1024;
  uint32_t steps_ = 28;
  float guidance_scale_ = 3.5f;
  int seed_ = -1;
  int max_sequence_length_ = -1;
  int num_images_per_prompt_ = 1;
  uint32_t num_generated_images_ = 0;

  std::vector<uint8_t> output_buffer_;

  // Forward-declared to avoid pulling OV headers into all consumers.
  // The pipeline is created in Init() and destroyed in Deinit().
  struct PipelineImpl;
  std::unique_ptr<PipelineImpl> pipeline_impl_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
