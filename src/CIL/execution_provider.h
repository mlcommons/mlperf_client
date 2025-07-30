#pragma once

#include <string>

namespace cil {

enum class EP {
  kIHVNativeOpenVINO,
  kIHVOrtGenAI,
  kIHVWindowsML,
  kIHVMetal,
  kIHVVulkan,
  kIHVCUDA,
  kIHVNativeQNN,
  kIHVMLX,
  kkIHVOrtGenAIRyzenAI,
  kUnknown
};

inline std::string EPToString(EP ep) {
  switch (ep) {
    case EP::kIHVNativeOpenVINO:
      return "IHV NativeOpenVINO";
    case EP::kIHVOrtGenAI:
      return "IHV OrtGenAI";
    case EP::kIHVWindowsML:
      return "IHV WindowsML";
    case EP::kIHVMetal:
      return "IHV Metal";
    case EP::kIHVVulkan:
      return "IHV Vulkan";
    case EP::kIHVCUDA:
      return "IHV CUDA";
    case EP::kIHVNativeQNN:
      return "NativeQNN";
    case EP::kIHVMLX:
      return "IHV MLX";
    case EP::kkIHVOrtGenAIRyzenAI:
      return "IHV OrtGenAI RyzenAI";
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
  if (ep_name == "IHV CUDA" || ep_name == "CUDA") return "llamacpp CUDA";
  if (ep_name == "IHV Vulkan" || ep_name == "Vulkan") return "llamacpp Vulkan";
  return ep_name;
}

inline std::string EPNameToDisplayName(const std::string& ep_name) {
  if (ep_name == "WindowsML") return "Windows ML";
  if (ep_name == "OrtGenAI-RyzenAI") return "ORT GenAI Ryzen AI";
  return ep_name;
}

inline bool IsEPFromGGML(EP ep) {
  switch (ep) {
    case EP::kIHVMetal:
    case EP::kIHVVulkan:
    case EP::kIHVCUDA:
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
    case EP::kIHVWindowsML:
      return "IHV/WindowsML";
    case EP::kIHVMetal:
      return "IHV/GGML";
    case EP::kIHVVulkan:
      return "IHV/GGML";
    case EP::kIHVCUDA:
      return "IHV/GGML";
    case EP::kIHVNativeQNN:
      return "IHV/NativeQNN";
    case EP::kIHVMLX:
      return "IHV/MLX";
    case EP::kkIHVOrtGenAIRyzenAI:
      return "IHV/OrtGenAI-RyzenAI";
    default:
      return "";
  }
}

inline std::string GetEPDependencySubdirPath(std::string ep_name) {
  return GetEPDependencySubdirPath(NameToEP(ep_name));
}

}  // namespace cil
