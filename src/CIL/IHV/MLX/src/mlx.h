#pragma once

#include "IHV_handler_class.h"

namespace cil {
namespace infer {
class BaseInferenceCommon;
}  // namespace infer
namespace IHV {

DECLARE_IHV_HANDLER_BASIC_CLASS(MLX, cil::infer::BaseInferenceCommon)

}  // namespace IHV
}  // namespace cil

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API_IHV

#ifdef __cplusplus
}
#endif