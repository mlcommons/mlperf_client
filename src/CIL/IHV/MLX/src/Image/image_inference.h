#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../api.h"
#include "base_inference_common.h"
#include "../mlx_config.h"

namespace cil {
namespace IHV {
namespace infer {

// txt2img (diffusion) inference for the MLX EP, e.g. FLUX.2 Klein. Mirrors the
// Diffusers EP's ImageInference contract (see src/CIL/IHV/Diffusers/src/Image/)
// so it plugs into the shared Txt2ImgExecutor, but runs natively on MLX/Metal
// via the Swift `img_*` bridge instead of embedded Python.
class ImageInference : public cil::infer::BaseInferenceCommon {
 public:
  ImageInference(const std::string& model_path, const std::string& model_name,
                 const MLXExecutionProviderSettings& ep_settings, Logger logger);
  ~ImageInference() override;

  void Init(const nlohmann::json& model_config);

  void Prepare() override;

  // Runs the denoising loop. `step_callback(step, total_steps)` fires once per
  // step; `submodule_callback(name, ms)` is reserved for per-component timings
  // (currently unused until the Swift pipeline reports them).
  void Run(const std::string& prompt, const std::string& negative_prompt,
           std::function<void(unsigned, unsigned)> step_callback,
           std::function<void(const std::string&, double)> submodule_callback =
               nullptr);

  void Reset() override;
  void Deinit() override;

  const uint8_t* GetOutputData() const { return output_buffer_.data(); }
  size_t GetOutputSize() const { return output_buffer_.size(); }
  uint32_t GetSteps() const { return steps_; }

 private:
  const MLXExecutionProviderSettings ep_settings_;

  uint32_t width_ = 1024;
  uint32_t height_ = 1024;
  uint32_t steps_ = 4;
  float guidance_scale_ = 1.0f;
  int64_t seed_ = -1;
  uint32_t num_images_per_prompt_ = 1;
  std::vector<int> seeds_;

  void* pipeline_ = nullptr;  // opaque Swift FluxImagePipeline handle
  std::vector<uint8_t> output_buffer_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
