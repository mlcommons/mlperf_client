#include "image_inference.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <openvino/genai/image_generation/text2image_pipeline.hpp>
#include <sstream>

namespace cil {
namespace IHV {
namespace infer {

struct ImageInference::PipelineImpl {
  std::unique_ptr<ov::genai::Text2ImagePipeline> pipeline;
};

ImageInference::ImageInference(
    const std::string& model_path, const std::string& model_name,
    const NativeOpenVINOExecutionProviderSettings& ep_settings,
    cil::Logger logger, const std::string& deps_dir)
    : BaseInference(model_path, model_name, ep_settings, logger, deps_dir) {}

ImageInference::~ImageInference() = default;

void ImageInference::Init(const nlohmann::json& model_config,
                          std::optional<API_IHV_DeviceID_t> device_id) {
  if (model_config.contains("image_generation")) {
    const auto& cfg = model_config["image_generation"];
    width_ = cfg.value("width", width_);
    height_ = cfg.value("height", height_);
    steps_ = cfg.value("num_inference_steps", steps_);
    guidance_scale_ = cfg.value("guidance_scale", guidance_scale_);
    seed_ = cfg.value("seed", seed_);
    max_sequence_length_ = cfg.value("max_sequence_length", max_sequence_length_);
    num_images_per_prompt_ = cfg.value("num_images_per_prompt", 1);
  }

  try {
    const std::string device =
        (device_id.has_value() && !devices_.empty() &&
         static_cast<size_t>(*device_id) < devices_.size())
            ? devices_[*device_id]
            : default_device_;

    std::filesystem::path file_path(model_path_);
    std::filesystem::path directory_path = file_path.parent_path();

    pipeline_impl_ = std::make_unique<PipelineImpl>();
    logger_(cil::LogLevel::kInfo,
            std::format("Creating Text2ImagePipeline on {} device from {}",
                        device, directory_path.string()));
    pipeline_impl_->pipeline = std::make_unique<ov::genai::Text2ImagePipeline>(
        directory_path.string(), device);

    logger_(cil::LogLevel::kInfo,
            "Text2ImagePipeline initialized on device: " + device);
  } catch (const std::exception& e) {
    SetErrorMessage(std::string("Failed to create Text2ImagePipeline: ") +
                    e.what());
    pipeline_impl_.reset();
  }
}

void ImageInference::Prepare() {
  output_buffer_.clear();
  num_generated_images_ = 0;
}

void ImageInference::Run(
    const std::string& prompt_json,
    std::function<void(unsigned, unsigned)> step_callback) {
  if (!pipeline_impl_ || !pipeline_impl_->pipeline) {
    SetErrorMessage("Pipeline not initialized");
    return;
  }

  try {
    nlohmann::json input = nlohmann::json::parse(prompt_json);
    std::string prompt = input.value("prompt", "");

    auto callback = [&step_callback, this](size_t step, size_t num_steps,
                                           ov::Tensor& /* latent */) -> bool {
      if (step_callback) {
        step_callback(static_cast<unsigned>(step),
                      static_cast<unsigned>(num_steps));
      }
      return false;
    };

    ov::AnyMap gen_props;
    gen_props[ov::genai::width.name()] = static_cast<int64_t>(width_);
    gen_props[ov::genai::height.name()] = static_cast<int64_t>(height_);
    gen_props[ov::genai::num_inference_steps.name()] =
        static_cast<size_t>(steps_);
    gen_props[ov::genai::num_images_per_prompt.name()] = 
        static_cast<size_t>(num_images_per_prompt_);
    gen_props[ov::genai::guidance_scale.name()] = guidance_scale_;
    gen_props[ov::genai::callback.name()] =
        std::function<bool(size_t, size_t, ov::Tensor&)>(callback);

    if (max_sequence_length_ > 0)
      gen_props[ov::genai::max_sequence_length.name()] = max_sequence_length_;

    if (seed_ >= 0)
      gen_props[ov::genai::rng_seed.name()] = static_cast<size_t>(seed_);

    if (guidance_scale_ > 1.0f && input.contains("negative_prompt"))
      gen_props[ov::genai::negative_prompt.name()] =
          input["negative_prompt"].get<std::string>();

    ov::Tensor image = pipeline_impl_->pipeline->generate(prompt, gen_props);

    const ov::Shape& shape = image.get_shape();
    if (shape.size() == 4 && shape[3] == 3) {
      size_t n = shape[0];
      size_t h = shape[1];
      size_t w = shape[2];

      if (n == 0 || h == 0 || w == 0) {
        SetErrorMessage("Unexpected output tensor shape: empty dimensions");
        output_buffer_.clear();
        num_generated_images_ = 0;
        return;
      }

      const size_t channels = 3;
      const size_t image_size = h * w * channels;
      const size_t buffer_size = n * image_size;
      const uint8_t* data = image.data<uint8_t>();
      output_buffer_.assign(data, data + buffer_size);
      width_ = static_cast<uint32_t>(w);
      height_ = static_cast<uint32_t>(h);
      num_generated_images_ = static_cast<uint32_t>(n);
    } else {
      SetErrorMessage(std::format("Unexpected output tensor shape: rank={}, "
                                  "last_dim={}",
                                  shape.size(),
                                  shape.empty() ? 0 : shape.back()));
      output_buffer_.clear();
      num_generated_images_ = 0;
    }
  } catch (const std::exception& e) {
    output_buffer_.clear();
    num_generated_images_ = 0;
    SetErrorMessage(std::string("Image generation failed: ") + e.what());
  }
}

void ImageInference::Reset() {
  // Output buffer stays valid until next Prepare()
}

void ImageInference::Deinit() {
  pipeline_impl_.reset();
  output_buffer_.clear();
  num_generated_images_ = 0;
}

const uint8_t* ImageInference::GetOutputData() const {
  return output_buffer_.empty() ? nullptr : output_buffer_.data();
}

size_t ImageInference::GetOutputSize() const { return output_buffer_.size(); }

uint32_t ImageInference::GetWidth() const { return width_; }

uint32_t ImageInference::GetHeight() const { return height_; }

uint32_t ImageInference::GetNumImages() const { return num_generated_images_; }

}  // namespace infer
}  // namespace IHV
}  // namespace cil
