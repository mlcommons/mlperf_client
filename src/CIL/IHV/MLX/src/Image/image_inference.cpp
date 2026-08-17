#include "image_inference.h"

#include <algorithm>
#include <filesystem>

namespace cil {
namespace IHV {
namespace infer {

namespace {
// Adapts the C step callback (void*, step, total) back to a std::function.
void StepCallbackThunk(void* ctx, uint32_t step, uint32_t total_steps) {
  auto* fn = static_cast<std::function<void(unsigned, unsigned)>*>(ctx);
  if (fn && *fn) (*fn)(step, total_steps);
}
}  // namespace

ImageInference::ImageInference(const std::string& model_path,
                               const std::string& model_name,
                               const MLXExecutionProviderSettings& ep_settings,
                               Logger logger)
    : BaseInferenceCommon(model_path, model_name, logger),
      ep_settings_(ep_settings) {
  device_type_ = ep_settings.GetDeviceType();
  if (device_type_.empty()) {
    device_type_ = "GPU";
  }
}

ImageInference::~ImageInference() { Deinit(); }

void ImageInference::Init(const nlohmann::json& model_config) {
  if (model_config.contains("image_generation")) {
    const auto& img_cfg = model_config["image_generation"];
    if (img_cfg.contains("width")) width_ = img_cfg["width"].get<uint32_t>();
    if (img_cfg.contains("height")) height_ = img_cfg["height"].get<uint32_t>();
    if (img_cfg.contains("num_inference_steps"))
      steps_ = img_cfg["num_inference_steps"].get<uint32_t>();
    if (img_cfg.contains("guidance_scale"))
      guidance_scale_ = img_cfg["guidance_scale"].get<float>();
    if (img_cfg.contains("seed")) seed_ = img_cfg["seed"].get<int>();
    if (img_cfg.contains("num_images_per_prompt"))
      num_images_per_prompt_ = img_cfg["num_images_per_prompt"].get<uint32_t>();
    if (img_cfg.contains("seeds"))
      seeds_ = img_cfg["seeds"].get<std::vector<int>>();
  }
  // Quantization variant (int8 / int4 / mixed) — a property of the packaged
  // model, so it is read from the model config rather than the EP settings.
  std::string quantization = model_config.value("quantization", std::string("int4"));

  // The model directory is the folder containing the pipeline artifacts. Like
  // the LLM path, model_path_ points at a config file inside that folder.
  std::filesystem::path model_path(model_path_);
  std::string model_dir =
      model_path.has_extension() ? model_path.parent_path().string() : model_path_;

  pipeline_ = img_create(model_dir.c_str(), model_name_.c_str(),
                         device_type_.c_str(), quantization.c_str());
  if (pipeline_ == nullptr) {
    SetErrorMessage("Failed to load MLX image pipeline for model '" +
                    model_name_ + "'");
  }
}

void ImageInference::Prepare() { output_buffer_.clear(); }

void ImageInference::Run(
    const std::string& prompt, const std::string& negative_prompt,
    std::function<void(unsigned, unsigned)> step_callback,
    std::function<void(const std::string&, double)> /*submodule_callback*/) {
  if (pipeline_ == nullptr) {
    SetErrorMessage("Image pipeline is not initialized");
    return;
  }

  const uint32_t num_images = std::max(1u, num_images_per_prompt_);
  output_buffer_.clear();

  for (uint32_t idx = 0; idx < num_images; ++idx) {
    const int64_t seed =
        seeds_.empty() ? seed_ : seeds_[idx % seeds_.size()];

    uint32_t out_width = 0;
    uint32_t out_height = 0;
    uint8_t* rgb = img_generate(
        pipeline_, prompt.c_str(), negative_prompt.c_str(), width_, height_,
        steps_, guidance_scale_, seed, &StepCallbackThunk, &step_callback,
        &out_width, &out_height);

    if (rgb == nullptr) {
      SetErrorMessage("Image generation failed");
      return;
    }

    const size_t size = static_cast<size_t>(out_width) * out_height * 3;
    output_buffer_.insert(output_buffer_.end(), rgb, rgb + size);
    img_free_image(rgb);
  }
}

void ImageInference::Reset() { output_buffer_.clear(); }

void ImageInference::Deinit() {
  if (pipeline_ != nullptr) {
    img_free_model(pipeline_);
    pipeline_ = nullptr;
  }
  output_buffer_.clear();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
