#ifndef TXT2IMG_EXECUTOR_H_
#define TXT2IMG_EXECUTOR_H_

#include "image_executor.h"

namespace cil {
namespace infer {

class Txt2ImgExecutor : public ImageExecutor {
 public:
  using ImageExecutor::ImageExecutor;

 protected:
  void RunScenario(EP ep, const nlohmann::json& settings) override;
};

}  // namespace infer
}  // namespace cil

#endif  // TXT2IMG_EXECUTOR_H_
