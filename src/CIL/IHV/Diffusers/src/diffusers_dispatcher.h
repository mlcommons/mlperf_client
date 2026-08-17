#pragma once

#include <memory>
#include <string>

#include "../../IHV.h"

class dylib;

namespace cil {
namespace IHV {

// Dispatcher for multi-vendor Diffusers builds: at Setup it reads the
// EP-config's backend/device_vendor, dylib-loads the matching EP library
// from a vendor subdir (e.g. "nvidia/Diffusers_NVIDIA.dll"), and forwards
// every API_IHV_* call. Pure C++ — no Python link. Only built when 2+
// vendors are enabled; single-vendor builds use the EP library directly.
class Diffusers_Dispatcher {
 public:
  ~Diffusers_Dispatcher();

  const struct API_IHV_Struct_t* Setup(const API_IHV_Setup_t* api);
  bool EnumerateDevices(struct API_IHV_DeviceEnumeration_t* api);
  int Init(const API_IHV_Init_t* api);
  int Prepare(const API_IHV_Simple_t* api);
  int Infer(struct API_IHV_Infer_t* api);
  int Reset(const API_IHV_Simple_t* api);
  int Deinit(const API_IHV_Deinit_t* api);
  void Release(const API_IHV_Release_t* api);

 private:
  std::unique_ptr<dylib> library_;

  API_IHV_Setup_func setup_ = nullptr;
  API_IHV_EnumerateDevices_func enumerate_ = nullptr;
  API_IHV_Init_func init_ = nullptr;
  API_IHV_Prepare_func prepare_ = nullptr;
  API_IHV_Infer_func infer_ = nullptr;
  API_IHV_Reset_func reset_ = nullptr;
  API_IHV_Deinit_func deinit_ = nullptr;
  API_IHV_Release_func release_ = nullptr;

  const struct API_IHV_Struct_t* ihv_struct_ = nullptr;
};

}  // namespace IHV
}  // namespace cil

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API_IHV

#ifdef __cplusplus
}
#endif
