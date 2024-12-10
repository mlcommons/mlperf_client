#pragma once

#include "IHV_handler_class.h"

namespace cil {
namespace IHV {

namespace infer {
class BaseInference;
}  // namespace infer

DECLARE_IHV_HANDLER_BASIC_CLASS(NativeOpenVINO, infer::BaseInference)

}  // namespace IHV
}  // namespace cil

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API_IHV

#ifdef __cplusplus
}
#endif