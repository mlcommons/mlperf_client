#include "base_inference.h"

#include "directml/adapter_enumeration.h"

namespace cil {
namespace IHV {
namespace infer {

BaseInference::BaseInference(
    const std::string& model_path, const std::string& ep_name,
    const std::string& deps_dir,
    const OrtGenAIExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInferenceCommon(model_path, logger),
      ep_settings_(ep_settings),
      ep_name_(ep_name),
      deps_dir_(deps_dir) {
  SetDeviceType(ep_settings.GetDeviceType());
  if (ep_name_.find("OrtGenAI") != std::string::npos) {
    cil::common::DirectML::DeviceEnumeration directml_device_enumeration;

    // Enumerate devices
    directml_devices_ = directml_device_enumeration.EnumerateDevices(
        deps_dir_, device_type_, ep_settings_.GetDeviceVendor());
  }
}

const API_IHV_DeviceList_t* const BaseInference::EnumerateDevices() {
  if (device_enum_ == nullptr) {
    if (ep_name_.find("OrtGenAI") != std::string::npos) {
      INIT_DIRECTML_DEVICE_ENUM(device_enum_, directml_devices_,
                                default_device_name_);

    } else {
      // Initialize list of devices with single device and default name
      // if it was never previously initialized
      default_device_name_ =
          std::format("{}.{}", ep_name_, ep_settings_.GetDeviceType());
      device_enum_ = [&]() -> DeviceListPtr {
        API_IHV_DeviceList_t* dev = AllocateDeviceList(1);

        dev->count = 1;
        dev->device_info_data[0].device_id = 0;
        std::strncpy(dev->device_info_data[0].device_name,
                     default_device_name_.c_str(),
                     API_IHV_MAX_DEVICE_NAME_LENGTH - 1);
        dev->device_info_data[0]
            .device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] = '\0';

        // Wrap raw pointer into unique ptr
        DeviceListPtr p(dev);
        return p;
      }();
    }
  }

  return device_enum_.get();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
