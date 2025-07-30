#pragma once

#include "base_inference_common.h"
#include "mlp_ort_genai_config.h"
#include "directml/adapter_enumeration.h"
#include "../../IHV.h"  // Include IHV API definitions

namespace cil {
namespace IHV {
namespace infer {

class BaseInference : public cil::infer::BaseInferenceCommon {
 public:
  BaseInference(const std::string& model_path, 
                const std::string& model_name,
                const std::string& ep_name,
                const std::string& deps_dir,
                const OrtGenAIExecutionProviderSettings& ep_settings,
                cil::Logger logger);

  const API_IHV_DeviceList_t *const EnumerateDevices() override;

 protected:
  DeviceListPtr device_enum_;
  std::string default_device_name_;

  std::optional<cil::common::DirectML::DeviceEnumeration::DeviceList> directml_devices_;

  const OrtGenAIExecutionProviderSettings ep_settings_;
  const std::string ep_name_;
  const std::string deps_dir_;

  std::string plugins_location_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
