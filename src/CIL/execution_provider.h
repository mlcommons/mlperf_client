#pragma once

#include <string>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif  //__APPLE__

namespace cil {

enum class EP {
  kIHVNativeOpenVINO,
  kIHVOrtGenAI,
  kkIHVOrtGenAIRyzenAI,
  kIHVWindowsML,
  kIHVLlamaCpp,
  kIHVNativeQNN,
  kIHVMLX,
  kIHVDiffusers,
  kUnknown
};

inline std::string EPToString(EP ep) {
  switch (ep) {
    case EP::kIHVNativeOpenVINO:
      return "IHV NativeOpenVINO";
    case EP::kIHVOrtGenAI:
      return "IHV OrtGenAI";
    case EP::kkIHVOrtGenAIRyzenAI:
      return "IHV OrtGenAI RyzenAI";
    case EP::kIHVWindowsML:
      return "IHV WindowsML";
    case EP::kIHVLlamaCpp:
      return "IHV LlamaCpp";
    case EP::kIHVNativeQNN:
      return "NativeQNN";
    case EP::kIHVMLX:
      return "IHV MLX";
    case EP::kIHVDiffusers:
      return "IHV Diffusers";
    default:
      return "Unknown";
  }
}

inline EP NameToEP(const std::string& ep_name) {
  if (ep_name == "IHV NativeOpenVINO" || ep_name == "NativeOpenVINO")
    return EP::kIHVNativeOpenVINO;
  if (ep_name == "IHV OrtGenAI" || ep_name == "OrtGenAI")
    return EP::kIHVOrtGenAI;
  if (ep_name == "IHV OrtGenAI RyzenAI" || ep_name == "OrtGenAI-RyzenAI")
    return EP::kkIHVOrtGenAIRyzenAI;
  if (ep_name == "IHV WindowsML" || ep_name == "WindowsML" ||
      ep_name == "WinML")
    return EP::kIHVWindowsML;
  if (ep_name == "llama-cpp" || ep_name == "LlamaCpp" ||
      ep_name == "IHV LlamaCpp" || ep_name == "IHV Metal" ||
      ep_name == "Metal" || ep_name == "IHV Vulkan" || ep_name == "Vulkan" ||
      ep_name == "IHV CUDA" || ep_name == "CUDA" || ep_name == "IHV ROCm" ||
      ep_name == "ROCm")
    return EP::kIHVLlamaCpp;
  if (ep_name == "NativeQNN") return EP::kIHVNativeQNN;
  if (ep_name == "MLX" || ep_name == "IHV MLX") return EP::kIHVMLX;
  if (ep_name == "Diffusers" || ep_name == "IHV Diffusers")
    return EP::kIHVDiffusers;
  return EP::kUnknown;
}

inline std::string EPNameToLongName(const std::string& ep_name) {
  if (ep_name == "IHV NativeOpenVINO" || ep_name == "NativeOpenVINO")
    return "Native OpenVINO";
  if (ep_name == "IHV NativeQNN" || ep_name == "NativeQNN") return "Native QNN";
  if (ep_name == "IHV OrtGenAI" || ep_name == "OrtGenAI") return "ORT GenAI";
  if (ep_name == "IHV OrtGenAI-RyzenAI" || ep_name == "OrtGenAI-RyzenAI")
    return "ORT GenAI Ryzen AI";
  if (ep_name == "IHV WindowsML" || ep_name == "WindowsML" ||
      ep_name == "Windows ML")
    return "Windows ML";
  if (ep_name == "llama-cpp" || ep_name == "LlamaCpp" ||
      ep_name == "IHV LlamaCpp")
    return "llama.cpp";
  if (ep_name == "IHV Metal" || ep_name == "Metal") return "llama.cpp Metal";
  if (ep_name == "IHV Vulkan" || ep_name == "Vulkan") return "llama.cpp Vulkan";
  if (ep_name == "IHV CUDA" || ep_name == "CUDA") return "llama.cpp CUDA";
  if (ep_name == "IHV ROCm" || ep_name == "ROCm") return "llama.cpp ROCm";
  if (ep_name == "IHV Diffusers" || ep_name == "Diffusers") return "Diffusers";
  return ep_name;
}

inline std::string EPNameToDisplayName(const std::string& ep_name) {
  if (ep_name == "OrtGenAI") return "ORT GenAI";
  if (ep_name == "OrtGenAI-RyzenAI") return "ORT GenAI Ryzen AI";
  if (ep_name == "NativeOpenVINO") return "Native OpenVINO";
  if (ep_name == "NativeQNN") return "Native QNN";
  if (ep_name == "WindowsML") return "Windows ML";
  if (ep_name == "Diffusers") return "Diffusers";
  if (ep_name == "llama-cpp" || ep_name == "LlamaCpp") return "llama.cpp";

  if (auto pos = ep_name.rfind('-'); pos != std::string::npos)
    return EPNameToDisplayName(ep_name.substr(0, pos)) + " " +
           ep_name.substr(pos + 1);

  return ep_name;
}

inline bool IsValidEP(EP ep) { return ep != EP::kUnknown; }

inline bool IsValidEP(const std::string& ep_name) {
  return IsValidEP(NameToEP(ep_name));
}

inline std::string GetEPDependencySubdirPath(EP ep) {
  switch (ep) {
    case EP::kIHVNativeOpenVINO:
      return "IHV/NativeOpenVINO";
    case EP::kIHVOrtGenAI:
      return "IHV/OrtGenAI";
    case EP::kkIHVOrtGenAIRyzenAI:
      return "IHV/OrtGenAI-RyzenAI";
    case EP::kIHVWindowsML:
      return "IHV/WindowsML";
    case EP::kIHVLlamaCpp:
      return "IHV/GGML";
    case EP::kIHVNativeQNN:
      return "IHV/NativeQNN";
    case EP::kIHVMLX:
      return "IHV/MLX";
    case EP::kIHVDiffusers:
      return "IHV/Diffusers";
    default:
      return "";
  }
}

inline std::string GetEPDependencySubdirPath(std::string ep_name) {
  return GetEPDependencySubdirPath(NameToEP(ep_name));
}

inline std::string GetEPEmbeddedLibraryName(EP ep) {
  switch (ep) {
    case EP::kIHVNativeOpenVINO:
#if defined(_WIN32)
      return "IHV/NativeOpenVINO/IHV_NativeOpenVINO.dll";
#else
      return "IHV/NativeOpenVINO/libIHV_NativeOpenVINO.so";
#endif  // _WIN32
    case EP::kIHVOrtGenAI:
      return "IHV/OrtGenAI/IHV_OrtGenAI.dll";
    case EP::kkIHVOrtGenAIRyzenAI:
      return "IHV/OrtGenAI-RyzenAI/IHV_OrtGenAI_RyzenAI.dll";
    case EP::kIHVWindowsML:
      return "IHV/WindowsML/IHV_WindowsML.dll";
    case EP::kIHVLlamaCpp:
#if TARGET_OS_IOS
      return "IHV_GGML_EPs.framework/IHV_GGML_EPs";
#elif defined(__APPLE__)
      return "libIHV_GGML_EPs.dylib";
#else
      return "IHV/GGML/IHV_GGML_EPs.dll";
#endif
    case EP::kIHVNativeQNN:
      return "IHV/NativeQNN/IHV_NativeQNN.dll";
    case EP::kIHVMLX:
#if TARGET_OS_IOS
      return "IHV_MLX.framework/IHV_MLX";
#else
      return "libIHV_MLX.dylib";
#endif
    case EP::kIHVDiffusers:
#if defined(_WIN32)
      return "IHV/Diffusers/IHV_Diffusers.dll";
#else
      return "libIHV_Diffusers.dylib";
#endif
    default:
      return "";
  }
}

}  // namespace cil
