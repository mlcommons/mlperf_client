#include "image_inference.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "../diffusers_config.h"

namespace fs = std::filesystem;

namespace cil {
namespace IHV {
namespace infer {

ImageInference::ImageInference(
    const std::string& model_path, const std::string& model_name,
    const std::string& ep_name, const std::string& deps_dir,
    const DiffusersExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInference(model_path, model_name, ep_name, deps_dir, ep_settings,
                    logger) {
  auto bridge_logger = [logger](int level, std::string msg) {
    logger(static_cast<cil::LogLevel>(level), std::move(msg));
  };
  python_bridge_ = std::make_unique<PythonBridge>(deps_dir, bridge_logger);
}

ImageInference::~ImageInference() {
  if (initialized_) Deinit();
}

const API_IHV_DeviceList_t* const ImageInference::EnumerateDevices() {
  if (device_enum_) return device_enum_.get();

  std::vector<PythonBridge::DeviceInfo> devices;
  bool ok =
      python_bridge_->EnumerateDevices(ep_settings_.GetBackend(), devices);
  if (!ok || devices.empty()) {
    // Probe failed (no torch, missing GPU, ...). Fall back to the base
    // class's stub so the harness still gets a non-null device list.
    return BaseInference::EnumerateDevices();
  }

  API_IHV_DeviceList_t* dev = AllocateDeviceList(devices.size());
  dev->count = devices.size();
  for (size_t i = 0; i < devices.size(); ++i) {
    dev->device_info_data[i].device_id = devices[i].device_id;
    std::string name = devices[i].name;
    if (devices[i].total_memory_gb > 0.0f) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), " (%.1f GB)", devices[i].total_memory_gb);
      name += buf;
    }
    std::strncpy(dev->device_info_data[i].device_name, name.c_str(),
                 API_IHV_MAX_DEVICE_NAME_LENGTH - 1);
    dev->device_info_data[i].device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] =
        '\0';
  }
  device_enum_ = DeviceListPtr(dev);
  default_device_name_ = devices.front().name;
  return device_enum_.get();
}

void ImageInference::Init(const nlohmann::json& model_config,
                          std::optional<API_IHV_DeviceID_t> device_id) {
  BaseInference::Init();
  if (!error_message_.empty()) return;

  if (initialized_) {
    error_message_ = "ImageInference already initialized";
    return;
  }

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

  int dev_id = ep_settings_.GetDeviceId().value_or(0);

  std::string effective_path = model_path_;

  std::map<std::string, std::string> component_files =
      ep_settings_.GetComponentFiles();
  component_files["__deps_dir__"] = deps_dir_;

  bool load_ok = python_bridge_->LoadPipeline(
      effective_path, ep_settings_.GetBackend(), dev_id, model_name_,
      ep_settings_.GetGpuMode(), ep_settings_.GetQuantization(), component_files,
      ep_settings_.GetPipelineClass(), ep_settings_.GetHfBaseId(),
      ep_settings_.GetOptimizationsJson());
  if (!load_ok) {
    error_message_ =
        "Pipeline load failed: " + python_bridge_->GetErrorMessage();
    return;
  }

  logger_(LogLevel::kInfo, "Diffusers ImageInference initialized (" +
                               std::to_string(width_) + "x" +
                               std::to_string(height_) + ", " +
                               std::to_string(steps_) + " steps)");

  initialized_ = true;
}

void ImageInference::Prepare() {
  if (!initialized_) {
    error_message_ = "ImageInference::Prepare called before Init";
    return;
  }
  output_buffer_.clear();
  error_message_.clear();
}

void ImageInference::Run(
    const std::string& prompt, const std::string& negative_prompt,
    std::function<void(unsigned, unsigned)> step_callback,
    std::function<void(const std::string&, double)> submodule_callback) {
  if (!initialized_) {
    error_message_ = "ImageInference::Run not initialized";
    return;
  }

  auto result = python_bridge_->GenerateImage(
      prompt, negative_prompt, width_, height_, steps_, guidance_scale_, seed_,
      num_images_per_prompt_, seeds_, step_callback, submodule_callback);

  if (result.pixel_data.empty()) {
    error_message_ =
        "Image generation failed: " + python_bridge_->GetErrorMessage();
    return;
  }

  output_buffer_ = std::move(result.pixel_data);
}

void ImageInference::Reset() {
  output_buffer_.clear();
  error_message_.clear();
}

void ImageInference::Deinit() {
  BaseInference::Deinit();
  if (!initialized_) return;

  if (python_bridge_) {
    python_bridge_->Unload();
  }
  output_buffer_.clear();
  initialized_ = false;
  error_message_.clear();
}

const uint8_t* ImageInference::GetOutputData() const {
  return output_buffer_.data();
}

size_t ImageInference::GetOutputSize() const { return output_buffer_.size(); }

}  // namespace infer
}  // namespace IHV
}  // namespace cil
