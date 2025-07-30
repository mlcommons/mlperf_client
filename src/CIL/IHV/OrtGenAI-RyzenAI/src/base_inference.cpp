#include "base_inference.h"

#include "directml/adapter_enumeration.h"

namespace cil {
namespace IHV {
namespace infer {

BaseInference::BaseInference(
    const std::string& model_path, const std::string& model_name,
    const std::string& ep_name, const std::string& deps_dir,
    const OrtGenAIExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInferenceCommon(model_path, model_name, logger),
      ep_settings_(ep_settings),
      ep_name_(ep_name),
      deps_dir_(deps_dir) {
  SetDeviceType(ep_settings.GetDeviceType());

  auto directml_gpu_devices =
      cil::common::DirectML::DeviceEnumeration().EnumerateDevices(deps_dir_,
                                                                  "GPU", "AMD");

  if (directml_gpu_devices.empty()) {
    logger(cil::LogLevel::kError, "No AMD GPU devices found.");
    return;
  }

  default_device_name_ =
      "AMD RyzenAI NPU +" + directml_gpu_devices.front().name + " GPU";
}

const API_IHV_DeviceList_t* const BaseInference::EnumerateDevices() {
  if (device_enum_ == nullptr) {
    API_IHV_DeviceList_t* dev = AllocateDeviceList(1);
    dev->count = 1;

    dev->device_info_data[0].device_id = 0;
    std::strncpy(dev->device_info_data[0].device_name,
                 default_device_name_.c_str(),
                 API_IHV_MAX_DEVICE_NAME_LENGTH - 1);
    dev->device_info_data[0].device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] =
        '\0';

    device_enum_ = DeviceListPtr(dev);
  }
  return device_enum_.get();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
