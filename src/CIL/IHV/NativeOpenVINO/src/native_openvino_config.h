#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#undef max

namespace cil {

namespace IHV {

class NativeOpenVINOExecutionProviderSettings {
 public:
  NativeOpenVINOExecutionProviderSettings() = default;
  inline NativeOpenVINOExecutionProviderSettings(const nlohmann::json& j) {
    FromJson(j);
  }
  ~NativeOpenVINOExecutionProviderSettings() = default;

  const std::string& GetDeviceType() const { return device_type_; }
  const std::optional<int>& GetNumOfThreads() const { return num_of_threads_; }
  const std::optional<int>& GetNumStreams() const { return num_streams_; }

  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  static void from_json(const nlohmann::json& j,
                        NativeOpenVINOExecutionProviderSettings& obj) {

    if (j.contains("device_type")) {
      j.at("device_type").get_to(obj.device_type_);
    }

    if (j.contains("num_of_threads")) {
      obj.num_of_threads_ = j.value("num_of_threads", 0);
    }

    if (j.contains("num_streams")) {
      obj.num_streams_ = j.value("num_streams", 0);
    }
  }

  static inline std::string GetDeviceType(const nlohmann::json& j) {
    std::string device_type;
    j.at("device_type").get_to(device_type);
    return device_type;
  }

 private:
  std::string device_type_;
  std::optional<int> num_of_threads_;
  std::optional<int> num_streams_;
};
}  // namespace IHV

}  // namespace cil