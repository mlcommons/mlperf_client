#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#undef max

namespace cil {

namespace IHV {

class WindowsMLExecutionProviderSettings {
 public:
  WindowsMLExecutionProviderSettings() = default;
  inline WindowsMLExecutionProviderSettings(const nlohmann::json& j) {
    FromJson(j);
  }
  ~WindowsMLExecutionProviderSettings() = default;

  const std::string& GetDeviceType() const { return device_type_; }
  const std::string& GetDeviceVendor() const { return device_vendor_; }
  const std::string& GetDeviceEP() const { return device_ep_; }
  const std::optional<int>& GetDeviceId() const { return device_id_; }

  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  static void from_json(const nlohmann::json& j,
                        WindowsMLExecutionProviderSettings& obj) {
    if (j.contains("device_type")) j.at("device_type").get_to(obj.device_type_);
    if (j.contains("device_id")) obj.device_id_ = j.value("device_id", 0);
    if (j.contains("device_vendor"))
      obj.device_vendor_ = j.value("device_vendor", "");
    if (j.contains("device_ep")) obj.device_ep_ = j.value("device_ep", "");
  }

  static inline std::string GetDeviceType(const nlohmann::json& j) {
    std::string device_type;
    j.at("device_type").get_to(device_type);
    return device_type;
  }

 private:
  std::string device_type_;
  std::string device_vendor_;
  std::string device_ep_;
  std::optional<int> device_id_;
};
}  // namespace IHV

}  // namespace cil