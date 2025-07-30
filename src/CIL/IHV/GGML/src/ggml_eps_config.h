#pragma once

#include <nlohmann/json.hpp>
#include <string>
#undef max

namespace cil {

namespace IHV {

class GGMLBasedExecutionProviderSettings {
 public:
  GGMLBasedExecutionProviderSettings() = default;
  inline GGMLBasedExecutionProviderSettings(const nlohmann::json& j) {
    FromJson(j);
  }
  ~GGMLBasedExecutionProviderSettings() = default;

  const std::string& GetDeviceType() const { return device_type_; }

  const std::optional<int>& GetGPULayers() const { return gpu_layers_; }

  const std::optional<bool>& GetFa() const { return fa_; }

  const std::optional<bool>& GetNoMmap() const { return no_mmap_; }

  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  static void from_json(const nlohmann::json& j,
                        GGMLBasedExecutionProviderSettings& obj) {
    if (j.contains("device_type")) j.at("device_type").get_to(obj.device_type_);
    if (j.contains("gpu_layers")) obj.gpu_layers_ = j.value("gpu_layers", 0);
    if (j.contains("fa")) 
      obj.fa_ = j.value("fa", 0) == 1;
    if (j.contains("no_mmap")) 
      obj.no_mmap_ = j.value("no_mmap", 0) == 1;
  }

 private:
  std::string device_type_;
  std::optional<int> gpu_layers_;
  std::optional<bool> fa_;
  std::optional<bool> no_mmap_;
};
}  // namespace IHV

}  // namespace cil