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
  kIHVMetal,
  kIHVVulkan,
  kIHVCUDA,
  kIHVROCm,
  kIHVNativeQNN,
  kIHVMLX,
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
    case EP::kIHVMetal:
      return "IHV Metal";
    case EP::kIHVVulkan:
      return "IHV Vulkan";
    case EP::kIHVCUDA:
      return "IHV CUDA";
    case EP::kIHVROCm:
      return "IHV ROCm";
    case EP::kIHVNativeQNN:
      return "NativeQNN";
    case EP::kIHVMLX:
      return "IHV MLX";
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
  if (ep_name == "IHV Metal" || ep_name == "Metal") return EP::kIHVMetal;
  if (ep_name == "IHV Vulkan" || ep_name == "Vulkan") return EP::kIHVVulkan;
  if (ep_name == "IHV CUDA" || ep_name == "CUDA") return EP::kIHVCUDA;
  if (ep_name == "IHV ROCm" || ep_name == "ROCm") return EP::kIHVROCm;
  if (ep_name == "NativeQNN") return EP::kIHVNativeQNN;
  if (ep_name == "MLX" || ep_name == "IHV MLX") return EP::kIHVMLX;
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
  if (ep_name == "IHV Metal" || ep_name == "Metal") return "llamacpp Metal";
  if (ep_name == "IHV Vulkan" || ep_name == "Vulkan") return "llamacpp Vulkan";
  if (ep_name == "IHV CUDA" || ep_name == "CUDA") return "llamacpp CUDA";
  if (ep_name == "IHV ROCm" || ep_name == "ROCm") return "llamacpp ROCm";
  return ep_name;
}

inline std::string EPNameToDisplayName(const std::string& ep_name) {
  if (ep_name == "OrtGenAI") return "ORT GenAI";
  if (ep_name == "OrtGenAI-RyzenAI") return "ORT GenAI Ryzen AI";
  if (ep_name == "NativeOpenVINO") return "Native OpenVINO";
  if (ep_name == "NativeQNN") return "Native QNN";
  if (ep_name == "WindowsML") return "Windows ML";
  return ep_name;
}

inline bool IsEPFromGGML(EP ep) {
  switch (ep) {
    case EP::kIHVMetal:
    case EP::kIHVVulkan:
    case EP::kIHVCUDA:
    case EP::kIHVROCm:
      return true;
    default:
      return false;
  }
}

inline bool IsEPFromGGML(const std::string& ep_name) {
  return IsEPFromGGML(NameToEP(ep_name));
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
    case EP::kIHVMetal:
      return "IHV/GGML";
    case EP::kIHVVulkan:
      return "IHV/GGML";
    case EP::kIHVCUDA:
      return "IHV/GGML";
    case EP::kIHVROCm:
      return "IHV/GGML";
    case EP::kIHVNativeQNN:
      return "IHV/NativeQNN";
    case EP::kIHVMLX:
      return "IHV/MLX";
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
    case EP::kIHVMetal:
#if TARGET_OS_IOS
      return "IHV_GGML_EPs.framework/IHV_GGML_EPs";
#else
      return "libIHV_GGML_EPs.dylib";
#endif
    case EP::kIHVVulkan:
#if __APPLE__
      return "libIHV_GGML_EPs.dylib";
#endif
    case EP::kIHVCUDA:
    case EP::kIHVROCm:
      return "IHV/GGML/IHV_GGML_EPs.dll";
    case EP::kIHVNativeQNN:
      return "IHV/NativeQNN/IHV_NativeQNN.dll";
    case EP::kIHVMLX:
#if TARGET_OS_IOS
      return "IHV_MLX.framework/IHV_MLX";
#else
      return "libIHV_MLX.dylib";
#endif
    default:
      return "";
  }
}

}  // namespace cil
