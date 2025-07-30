#include "base_inference_common.h"

namespace cil::infer {

void BaseInferenceCommon::LogTime(
    const std::string& desc,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& st,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& et)
    const {
  auto duration_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(et - st).count();
  double duration_ms = (double)duration_ns / 1e6;
  logger_(LogLevel::kInfo, desc + std::to_string(duration_ms) + " ms\n");
}

BaseInferenceCommon::BaseInferenceCommon(const std::string& model_path,
                                         const std::string& model_name,
                                         Logger logger)
    : model_path_(model_path), model_name_(model_name), logger_(logger) {}

std::string BaseInferenceCommon::GetDeviceType() const { return device_type_; }

std::string BaseInferenceCommon::GetErrorMessage() const {
  return error_message_;
}

Logger BaseInferenceCommon::GetLogger() const { return logger_; }

void BaseInferenceCommon::SetDeviceType(const std::string& device_type) {
  device_type_ = device_type;
}

void BaseInferenceCommon::SetErrorMessage(const std::string& error_message) {
  error_message_ = error_message;
}

const API_IHV_DeviceList_t *const BaseInferenceCommon::EnumerateDevices() {
  static DeviceListPtr dev_list = []() -> DeviceListPtr {
    API_IHV_DeviceList_t *dl = AllocateDeviceList(1);

    dl->count = 1;
    dl->device_info_data[0].device_id = 0;
    dl->device_info_data[0].device_name[0] = '0';

    // Wrap device list pointer unto unique ptr with std::free deleter
    DeviceListPtr p(dl);
    return p;
  }();

  return dev_list.get();
}


}  // namespace cil::infer
