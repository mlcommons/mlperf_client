#pragma once

#include "IHV_handler_class.h"

namespace cil {
namespace IHV {

#define API_IHV_ORT_GENAI "OrtGenAI-RyzenAI"
#define API_IHV_ORT_GENAI_VERSION "2025.6"  // ORT GenAI Library version

namespace infer {
class BaseInference;
}  // namespace infer

DECLARE_IHV_HANDLER_BASIC_CLASS(OrtGenAIRyzenAI, infer::BaseInference)

}  // namespace IHV
}  // namespace cil

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_API_IHV

#ifdef __cplusplus
}
#endif