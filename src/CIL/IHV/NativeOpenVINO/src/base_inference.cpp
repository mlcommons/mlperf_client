#include "base_inference.h"

#include <openvino/genai/llm_pipeline.hpp>
#include <openvino/openvino.hpp>

#define NATIVE_OPENVINO_PLUGINS_PATH "plugins.xml"

namespace cil {
namespace IHV {
namespace infer {

std::string getPluginsLocation(const std::string& deps_dir) {
  try {
    auto plugins_location =
        std::filesystem::absolute(std::filesystem::path(deps_dir) /
                                  NATIVE_OPENVINO_PLUGINS_PATH)
            .string();

    if (std::filesystem::exists(plugins_location)) {
      return plugins_location;
    }
  } catch (const std::exception& e) {
    return "";
  } catch (...) {
    return "";
  }
  return "";
}

BaseInference::BaseInference(
    const std::string& model_path,
    const NativeOpenVINOExecutionProviderSettings& ep_settings,
    cil::Logger logger, const std::string& deps_dir)
    : BaseInferenceCommon(model_path, logger), ep_settings_(ep_settings) {
  plugins_location_ = getPluginsLocation(deps_dir);
  if (plugins_location_ == "") {
    error_message_ = "Plugins file not found: " + plugins_location_;
    return;
  }
  try {
    // Create an instance of the OpenVINO core.
    core_ = std::make_unique<ov::Core>(plugins_location_);

    auto devices = core_->get_available_devices();
    if (devices.empty()) {
      error_message_ = "No devices found";
      return;
    }
    auto device_type = ep_settings_.GetDeviceType();

    auto it = std::find(devices.begin(), devices.end(), device_type);
    if (it == devices.end()) {
      std::string all_devs;
      for (const auto& device : devices) {
        all_devs = all_devs + " " + device;
      }
      error_message_ = "IHV BaseInference: " + device_type +
                       " device not found. Use: " + all_devs;
    }
  } catch (const std::exception& e) {
    error_message_ = e.what();
  } catch (...) {
    error_message_ = "Unknown error";
  }
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
