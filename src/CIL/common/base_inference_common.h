#ifndef BASE_INFERENCE_COMMON_H_
#define BASE_INFERENCE_COMMON_H_

#include <chrono>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace cil::infer {

class BaseInferenceCommon {
 public:
  BaseInferenceCommon(const std::string& model_path, Logger logger);
  virtual ~BaseInferenceCommon() = default;

  void LogTime(
      const std::string& desc,
      const std::chrono::time_point<std::chrono::high_resolution_clock>& st,
      const std::chrono::time_point<std::chrono::high_resolution_clock>& et)
      const;

  virtual void Init(){
      // Override this when needed
  };
  virtual void Prepare(){
      // Override this when needed
  };
  virtual void Reset(){
      // Override this when needed
  };
  virtual void Deinit(){
      // Override this when needed
  };

  std::string GetDeviceType() const;
  std::string GetErrorMessage() const;

  void SetDeviceType(const std::string& device_type);
  void SetErrorMessage(const std::string& error_message);

  Logger GetLogger() const;

 protected:
  std::string model_path_;
  // Logger
  Logger logger_;

  // Results
  std::string device_type_;
  std::string error_message_;
};
}  // namespace cil::infer

#endif  // !BASE_INFERENCE_COMMON_H_
