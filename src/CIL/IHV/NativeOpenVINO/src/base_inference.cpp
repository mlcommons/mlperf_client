#include "base_inference.h"

#include <cstdlib>
#include <format>
#include <openvino/genai/llm_pipeline.hpp>
#include <openvino/openvino.hpp>
#include <string_view>
#include <unordered_set>

namespace {

constexpr std::string_view kPluginFileName = "plugins.xml";

std::string GetFullDeviceId(std::string_view base, auto id) {
  return std::format("{}.{}", base, id);
}

// Create set of required devices and aliases.
std::unordered_set<std::string> GetDeviceNames(
    const std::string& device_type, std::optional<size_t> device_id) {
  std::unordered_set<std::string> devs;

  const auto dev_id = device_id.value_or(0);
  devs.insert(GetFullDeviceId(device_type, dev_id));
  if (dev_id == 0) {
    devs.insert(device_type);
  }
  return devs;
}

// Concatenate string from iteratable container
std::string ConcatStrings(auto values) {
  std::string s;
  for (const auto& v : values) {
    s = std::format("{} {}", s, v);
  }
  return s;
}

std::string getPluginsLocation(std::string_view deps_dir) {
  try {
    auto plugins_location =
        std::filesystem::absolute(std::filesystem::path(deps_dir) /
                                  kPluginFileName)
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

}  // namespace

namespace cil {
namespace IHV {
namespace infer {

BaseInference::BaseInference(
    const std::string& model_path,
    const NativeOpenVINOExecutionProviderSettings& ep_settings,
    cil::Logger logger, const std::string& deps_dir)
    : BaseInferenceCommon(model_path, logger), ep_settings_(ep_settings) {
  plugins_location_ = getPluginsLocation(deps_dir);
  if (plugins_location_.empty()) {
    error_message_ = "Plugins file not found: " + plugins_location_;
    return;
  }
  device_type_ = ep_settings_.GetDeviceType();

  try {
    // Create an instance of the OpenVINO core.
    std::unique_ptr<ov::Core> core =
        std::make_unique<ov::Core>(plugins_location_);

    // List available devices by device type
    const auto devices = core->get_available_devices();
    for (const auto& d : devices) {
      if (d.substr(0, device_type_.size()) == device_type_) {
        devices_.emplace_back(d);
      }
    }

    // Get full device name
    device_names_.reserve(devices_.size());
    for (const auto& device : devices_) {
      device_names_.emplace_back(
          core->get_property(device, ov::device::full_name));
    }

    if (devices_.empty()) {
      error_message_ = std::format("No {} devices found.", device_type_);
      return;
    }

    // Get list of device names with aliases
    const auto dev_names =
        GetDeviceNames(device_type_, ep_settings_.GetDeviceId());
    // See if device available in the list
    auto it =
        std::find_if(devices_.begin(), devices_.end(),
                     [&](const auto& d) { return dev_names.contains(d); });

    if (it == devices_.end()) {
      error_message_ =
          "IHV Native OpenVINO (BaseInference):" + ConcatStrings(dev_names) +
          " device not found. Use:" + ConcatStrings(devices_);
      return;
    }

    default_device_ = *it;

  } catch (const std::exception& e) {
    error_message_ = std::format("Error initializing OpenVINO: {}", e.what());
  } catch (...) {
    error_message_ = "Unknown error";
  }
}

const API_IHV_DeviceList_t* const BaseInference::EnumerateDevices() {
  if (nullptr != device_list_) {
    // Return existing list of initialized devices
    return device_list_.get();
  }

  // Allocate the structure and fill it with device specific info
  API_IHV_DeviceList_t* dl = AllocateDeviceList(devices_.size());

  dl->count = devices_.size();
  for (int i = 0; i < devices_.size(); i++) {
    dl->device_info_data[i].device_id = i;
    std::strncpy(dl->device_info_data[i].device_name, device_names_[i].c_str(),
                 API_IHV_MAX_DEVICE_NAME_LENGTH - 1);
    dl->device_info_data[i].device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] =
        '\0';
  }
  device_list_.reset(dl);

  return device_list_.get();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
