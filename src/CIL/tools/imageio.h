#ifndef CIL_TOOLS_IMAGEIO_H_
#define CIL_TOOLS_IMAGEIO_H_

#include <cstdint>
#include <memory>
#include <string>

#include "tools/iimageio.h"

namespace cil::tools {

class ImageIO {
 public:
  explicit ImageIO(std::string& error);
  ~ImageIO();

  ImageIO(const ImageIO&) = delete;
  ImageIO& operator=(const ImageIO&) = delete;
  ImageIO(ImageIO&&) = delete;
  ImageIO& operator=(ImageIO&&) = delete;

  bool SavePNG(const std::string& path, const uint8_t* data, uint32_t width,
               uint32_t height, uint32_t channels = 3) const;

  bool SaveJPG(const std::string& path, const uint8_t* data, uint32_t width,
               uint32_t height, uint32_t channels = 3,
               int quality = 95) const;

  bool SaveBMP(const std::string& path, const uint8_t* data, uint32_t width,
               uint32_t height, uint32_t channels = 3) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_IMAGEIO_H_
