#pragma once

#include "IHV_handler_class.h"

namespace cil {
namespace IHV {

#define API_IHV_DIFFUSERS "Diffusers"
#define API_IHV_DIFFUSERS_VERSION "0.1.0"

namespace infer {
class BaseInference;
}  // namespace infer

DECLARE_IHV_HANDLER_BASIC_CLASS(Diffusers, infer::BaseInference)

}  // namespace IHV
}  // namespace cil

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API_IHV

#ifdef __cplusplus
}
#endif
