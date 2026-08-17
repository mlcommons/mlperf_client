#ifndef CIL_TOOLS_IIMAGEIO_H_
#define CIL_TOOLS_IIMAGEIO_H_

#include <cstdint>
#include <string>

namespace cil::tools {

class IImageIO {
 public:
  virtual ~IImageIO() = default;

  virtual bool SavePNG(const std::string& path, const uint8_t* data,
                       uint32_t width, uint32_t height,
                       uint32_t channels = 3) const = 0;

  virtual bool SaveJPG(const std::string& path, const uint8_t* data,
                       uint32_t width, uint32_t height,
                       uint32_t channels = 3, int quality = 95) const = 0;

  virtual bool SaveBMP(const std::string& path, const uint8_t* data,
                       uint32_t width, uint32_t height,
                       uint32_t channels = 3) const = 0;
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_IIMAGEIO_H_
