#ifndef BASE_INFERENCE_COMMON_H_
#define BASE_INFERENCE_COMMON_H_

#include <chrono>
#include <nlohmann/json.hpp>

#include "../IHV/IHV.h"  // Include IHV API definitions
#include "logger.h"

namespace cil::infer {

class BaseInferenceCommon {
 public:
  struct DeviceListDeleter {
    void operator()(API_IHV_DeviceList_t* value) { std::free(value); }
  };

  using DeviceListPtr =
      std::unique_ptr<API_IHV_DeviceList_t, DeviceListDeleter>;

  BaseInferenceCommon(const std::string& model_path, const std::string& model_name, Logger logger);
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
  // Override this when needed
  virtual const API_IHV_DeviceList_t* const EnumerateDevices();

  std::string GetDeviceType() const;
  std::string GetErrorMessage() const;

  void SetDeviceType(const std::string& device_type);
  void SetErrorMessage(const std::string& error_message);

  Logger GetLogger() const;

 protected:
  static size_t GetDeviceListSize(size_t count) {
    return sizeof(API_IHV_DeviceList_t) + count * sizeof(API_IHV_DeviceInfo_t);
  }

  static API_IHV_DeviceList_t* AllocateDeviceList(size_t count) {
    return static_cast<API_IHV_DeviceList_t*>(malloc(GetDeviceListSize(count)));
  }

#define INIT_DIRECTML_DEVICE_ENUM(DEVICE_ENUM_VAR, DIRECTML_DEVICES, \
                                  DEFAULT_DEVICE_NAME)               \
  {                                                                  \
    auto devices_ = (DIRECTML_DEVICES).value();                      \
    API_IHV_DeviceList_t* dev = AllocateDeviceList(devices_.size()); \
    dev->count = devices_.size();                                    \
    for (size_t i = 0; i < devices_.size(); ++i) {                   \
      dev->device_info_data[i].device_id = i;                        \
      std::strncpy(dev->device_info_data[i].device_name,             \
                   devices_[i].name.c_str(),                         \
                   API_IHV_MAX_DEVICE_NAME_LENGTH - 1);              \
      dev->device_info_data[i]                                       \
          .device_name[API_IHV_MAX_DEVICE_NAME_LENGTH - 1] = '\0';   \
    }                                                                \
    (DEVICE_ENUM_VAR) = DeviceListPtr(dev);                          \
    DEFAULT_DEVICE_NAME = devices_.empty() ? "" : devices_[0].name;  \
  }  // namespace cil::infer

  std::string model_path_;
  std::string model_name_;
  // Logger
  Logger logger_;

  // Results
  std::string device_type_;
  std::string error_message_;
};
}  // namespace cil::infer

#endif  // !BASE_INFERENCE_COMMON_H_
