#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cil {
namespace IHV {

using BridgeLogger = std::function<void(int, std::string)>;

class PythonBridge {
 public:
  PythonBridge(const std::string& deps_dir, BridgeLogger logger);
  ~PythonBridge();

  PythonBridge(const PythonBridge&) = delete;
  PythonBridge& operator=(const PythonBridge&) = delete;

  bool IsInitialized() const { return initialized_; }
  const std::string& GetErrorMessage() const { return error_message_; }

  bool LoadPipeline(
      const std::string& model_path, const std::string& backend, int device_id,
      const std::string& model_base_name = "", const std::string& gpu_mode = "",
      const std::string& quantization = "",
      const std::map<std::string, std::string>& component_files = {},
      const std::string& pipeline_class = "",
      const std::string& hf_base_id = "",
      const std::string& optimizations_json = "");

  struct DeviceInfo {
    int device_id = 0;
    std::string name;
    float total_memory_gb = 0.0f;
  };

  bool EnumerateDevices(const std::string& backend,
                        std::vector<DeviceInfo>& out);

  struct ImageResult {
    std::vector<uint8_t> pixel_data;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  ImageResult GenerateImage(
      const std::string& prompt, const std::string& negative_prompt,
      uint32_t width, uint32_t height, uint32_t num_inference_steps,
      float guidance_scale, int seed, uint32_t num_images_per_prompt,
      const std::vector<int>& seeds,
      std::function<void(unsigned, unsigned)> step_callback,
      std::function<void(const std::string&, double)> submodule_callback =
          nullptr);

  void Unload();

 private:
  bool InitInterpreter(const std::string& backend = "");
  static std::filesystem::path ResolvePythonHome(const std::string& deps_dir,
                                                 const std::string& backend);
  bool EnsurePackageInstalled();
  void ShutdownInterpreter();

  std::string deps_dir_;
  BridgeLogger logger_;
  bool initialized_ = false;
  bool interpreter_started_ = false;
  bool package_installed_ = false;
  std::string error_message_;
};

}  // namespace IHV
}  // namespace cil
