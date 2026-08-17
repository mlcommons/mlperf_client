#ifndef CIL_TOOLS_IMAGEIO_H_
#define CIL_TOOLS_IMAGEIO_H_

#include <memory>

#include "tools/iimageio.h"

namespace cil::tools {

class ImageIO : public IImageIO {
 public:
  static std::unique_ptr<ImageIO> Create(std::string& error);

  ~ImageIO() override;

  bool SavePNG(const std::string& path, const uint8_t* data, uint32_t width,
               uint32_t height, uint32_t channels = 3) const override;

  bool SaveJPG(const std::string& path, const uint8_t* data, uint32_t width,
               uint32_t height, uint32_t channels = 3,
               int quality = 95) const override;

  bool SaveBMP(const std::string& path, const uint8_t* data, uint32_t width,
               uint32_t height, uint32_t channels = 3) const override;

 private:
  ImageIO() = default;
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_IMAGEIO_H_
