#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "../../IHV.h"
#include "../base_inference.h"
#include "../python_bridge.h"

namespace cil {
namespace IHV {
namespace infer {

class ImageInference : public BaseInference {
 public:
  ImageInference(const std::string& model_path, const std::string& model_name,
                 const std::string& ep_name, const std::string& deps_dir,
                 const DiffusersExecutionProviderSettings& ep_settings,
                 cil::Logger logger);
  ~ImageInference() override;

  const API_IHV_DeviceList_t* const EnumerateDevices() override;

  void Init(const nlohmann::json& model_config,
            std::optional<API_IHV_DeviceID_t> device_id);

  void Prepare() override;

  void Run(const std::string& prompt, const std::string& negative_prompt,
           std::function<void(unsigned, unsigned)> step_callback,
           std::function<void(const std::string&, double)> submodule_callback =
               nullptr);

  void Reset() override;
  void Deinit() override;

  const uint8_t* GetOutputData() const;
  size_t GetOutputSize() const;
  uint32_t GetSteps() const { return steps_; }

 private:
  uint32_t width_ = 1024;
  uint32_t height_ = 1024;
  uint32_t steps_ = 4;
  float guidance_scale_ = 1.0f;
  int seed_ = -1;
  uint32_t num_images_per_prompt_ = 4;
  std::vector<int> seeds_;

  std::unique_ptr<PythonBridge> python_bridge_;
  std::vector<uint8_t> output_buffer_;
  bool initialized_ = false;
};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
