#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace cil {
namespace IHV {

class DiffusersExecutionProviderSettings {
 public:
  DiffusersExecutionProviderSettings() = default;
  inline DiffusersExecutionProviderSettings(const nlohmann::json& j) {
    FromJson(j);
  }
  ~DiffusersExecutionProviderSettings() = default;

  // GPU / NPU -- shown by the harness.
  const std::string& GetDeviceType() const { return device_type_; }
  // CUDA / MPS -- selects the torch device.
  const std::string& GetBackend() const { return backend_; }
  const std::optional<int>& GetDeviceId() const { return device_id_; }
  const std::string& GetGpuMode() const { return gpu_mode_; }
  const std::string& GetQuantization() const { return quantization_; }
  const std::map<std::string, std::string>& GetComponentFiles() const {
    return component_files_;
  }
  const std::string& GetPipelineClass() const { return pipeline_class_; }
  const std::string& GetHfBaseId() const { return hf_base_id_; }
  // Raw JSON string of the optimizations array, forwarded to Python as-is.
  const std::string& GetOptimizationsJson() const {
    return optimizations_json_;
  }

  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  static void from_json(const nlohmann::json& j,
                        DiffusersExecutionProviderSettings& obj) {
    if (j.contains("backend")) j.at("backend").get_to(obj.backend_);
    if (j.contains("device_type")) j.at("device_type").get_to(obj.device_type_);
    if (obj.device_type_.empty()) {
      obj.device_type_ = "GPU";
    }
    if (j.contains("device_id")) obj.device_id_ = j.value("device_id", 0);
    if (j.contains("gpu_mode")) j.at("gpu_mode").get_to(obj.gpu_mode_);
    if (j.contains("fp8_layerwise") && j["fp8_layerwise"].get<bool>())
      obj.quantization_ = "torchao_fp8";
    if (j.contains("quantization"))
      j.at("quantization").get_to(obj.quantization_);
    if (j.contains("components")) {
      for (auto& [key, val] : j["components"].items()) {
        obj.component_files_[key] = val.get<std::string>();
      }
    }
    if (j.contains("pipeline")) j.at("pipeline").get_to(obj.pipeline_class_);
    if (j.contains("hf_base_id")) j.at("hf_base_id").get_to(obj.hf_base_id_);
    if (j.contains("optimizations") && j["optimizations"].is_array())
      obj.optimizations_json_ = j["optimizations"].dump();
  }

 private:
  std::string backend_ = "CUDA";
  std::string device_type_ = "GPU";
  std::optional<int> device_id_;
  std::string gpu_mode_;
  std::string quantization_;
  std::map<std::string, std::string> component_files_;
  std::string pipeline_class_;
  std::string hf_base_id_;
  std::string optimizations_json_;
};

}  // namespace IHV
}  // namespace cil
