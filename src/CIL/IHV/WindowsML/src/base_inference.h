#pragma once

#include "../../IHV.h"  // Include IHV API definitions
#include "base_inference_common.h"
#include "directml/adapter_enumeration.h"
#include "windowsml_config.h"
#include "win_onnxruntime_cxx_api.h"

namespace cil {
namespace IHV {
namespace infer {

class BaseInference : public cil::infer::BaseInferenceCommon {
 public:
  BaseInference(const std::string& model_path, const std::string& model_name,
                const std::string& ep_name, const std::string& deps_dir,
                const WindowsMLExecutionProviderSettings& ep_settings,
                cil::Logger logger);

  ~BaseInference() override;

  const API_IHV_DeviceList_t* const EnumerateDevices() override;

 protected:
  void DetectWindowsMLDevices(
      Ort::Env& env, const std::vector<std::pair<std::wstring, std::wstring>>&
                                  execution_provider_paths);
  void AssignModelForDevices();

  DeviceListPtr device_enum_;
  std::string default_device_name_;

  struct WinMLDevice {
    std::string ep;
    std::string type;
    std::string name;
    uint32_t id;
    std::string model_path;

    std::optional<unsigned> directml_device_entry;
  };

  std::optional<std::vector<WinMLDevice>> winml_devices_;

  // This is needed to find the actual device id for DirectML devices
  std::optional<cil::common::DirectML::DeviceEnumeration::DeviceList>
      directml_devices_;

  const WindowsMLExecutionProviderSettings ep_settings_;
  const std::string ep_name_;
  const std::string deps_dir_;

  std::string plugins_location_;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
