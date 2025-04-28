#pragma once

#include <string>

namespace cil {

enum class EP {
  kIHVNativeOpenVINO,
  kIHVOrtGenAI,
  kUnknown
};

inline std::string EPToString(EP ep) {
  switch (ep) {
    case EP::kIHVNativeOpenVINO:
      return "IHV NativeOpenVINO";
    case EP::kIHVOrtGenAI:
      return "IHV OrtGenAI";
    default:
      return "Unknown";
  }
}

inline EP NameToEP(const std::string& ep_name) {
  if (ep_name == "IHV NativeOpenVINO" || ep_name == "NativeOpenVINO")
    return EP::kIHVNativeOpenVINO;
  if (ep_name == "IHV OrtGenAI" || ep_name == "OrtGenAI")
    return EP::kIHVOrtGenAI;
  return EP::kUnknown;
}

inline bool IsEPFromKnownIHV(EP ep) {
  switch (ep) {
    case EP::kUnknown:
      return false;
    default:
      return true;
  }
}

inline bool IsEPFromKnownIHV(const std::string& ep_name) {
  return IsEPFromKnownIHV(NameToEP(ep_name));
}

inline std::string GetEPDependencySubdirPath(EP ep) {
  switch (ep) {
    case EP::kIHVNativeOpenVINO:
      return "IHV/NativeOpenVINO";
    case EP::kIHVOrtGenAI:
      return "IHV/OrtGenAI";
    default:
      return "";
  }
}

inline std::string GetEPDependencySubdirPath(std::string ep_name) {
  return GetEPDependencySubdirPath(NameToEP(ep_name));
}

}  // namespace cil
