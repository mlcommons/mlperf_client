#pragma once

#include <openvino/genai/llm_pipeline.hpp>
#include <openvino/openvino.hpp>

#include "base_inference_common.h"
#include "native_openvino_config.h"

namespace cil {
namespace IHV {
namespace infer {

class BaseInference : public cil::infer::BaseInferenceCommon {
 public:
  BaseInference(const std::string& model_path,
                const NativeOpenVINOExecutionProviderSettings& ep_settings,
                cil::Logger logger, const std::string& deps_dir);

  const API_IHV_DeviceList_t *const EnumerateDevices() override;

 protected:
  const NativeOpenVINOExecutionProviderSettings ep_settings_;

  std::string device_type_;

  std::string plugins_location_;

  DeviceListPtr device_list_;

  std::vector<std::string> devices_;       // List of devices for device_type_
  std::vector<std::string> device_names_;  // Full names
  std::string default_device_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
