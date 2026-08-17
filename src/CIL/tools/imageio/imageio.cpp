#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "imageio.h"

#include "stb_image_write.h"

#ifdef _WIN32
#ifdef IMAGEIO_BUILDING
#define IMAGEIO_API __declspec(dllexport)
#else
#define IMAGEIO_API __declspec(dllimport)
#endif
#else
#define IMAGEIO_API __attribute__((visibility("default")))
#endif

namespace cil::tools {

std::unique_ptr<ImageIO> ImageIO::Create(std::string& error) {
  return std::unique_ptr<ImageIO>(new ImageIO());
}

ImageIO::~ImageIO() = default;

bool ImageIO::SavePNG(const std::string& path, const uint8_t* data,
                      uint32_t width, uint32_t height,
                      uint32_t channels) const {
  if (!data || width == 0 || height == 0) return false;
  int stride = static_cast<int>(width * channels);
  return stbi_write_png(path.c_str(), static_cast<int>(width),
                        static_cast<int>(height), static_cast<int>(channels),
                        data, stride) != 0;
}

bool ImageIO::SaveJPG(const std::string& path, const uint8_t* data,
                      uint32_t width, uint32_t height, uint32_t channels,
                      int quality) const {
  if (!data || width == 0 || height == 0) return false;
  return stbi_write_jpg(path.c_str(), static_cast<int>(width),
                        static_cast<int>(height), static_cast<int>(channels),
                        data, quality) != 0;
}

bool ImageIO::SaveBMP(const std::string& path, const uint8_t* data,
                      uint32_t width, uint32_t height,
                      uint32_t channels) const {
  if (!data || width == 0 || height == 0) return false;
  return stbi_write_bmp(path.c_str(), static_cast<int>(width),
                        static_cast<int>(height), static_cast<int>(channels),
                        data) != 0;
}

}  // namespace cil::tools

extern "C" {

IMAGEIO_API cil::tools::IImageIO* create_imageio() {
  std::string error;
  if (auto io = cil::tools::ImageIO::Create(error); io) return io.release();
  return nullptr;
}

IMAGEIO_API void destroy_imageio(cil::tools::IImageIO* ptr) { delete ptr; }

}  // extern "C"
