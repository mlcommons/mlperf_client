#pragma once

#include <memory>
#include <string>

#include "../../IHV.h"  // Include IHV API definitions

class dylib;

namespace cil {
namespace IHV {

// GGML_EPs_Handler class works as a bootloader, which loads and uses the
// corresponding library based on the EP name and encasulates it in the IHV API.
class GGML_EPs_Handler {
 public:
  ~GGML_EPs_Handler();

  const struct API_IHV_Struct_t* Setup(const API_IHV_Setup_t* api);
  bool EnumerateDevices(struct API_IHV_DeviceEnumeration_t* api);
  int Init(const API_IHV_Init_t* api);
  int Prepare(const API_IHV_Simple_t* api);
  int Infer(struct API_IHV_Infer_t* api);
  int Reset(const API_IHV_Simple_t* api);
  int Deinit(const API_IHV_Deinit_t* api);
  void Release(const API_IHV_Release_t* api);

 private:
  template <typename FunctionType>
  FunctionType LoadFn(const std::string& name, API_IHV_Logger_func logger,
                      void* context);

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