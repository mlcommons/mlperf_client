#include "imageio.h"

#include <log4cxx/logger.h>

#include <dylib.hpp>

#include "unpacker.h"
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

static const log4cxx::LoggerPtr logger_tools(
    log4cxx::Logger::getLogger("tools::ImageIO"));

namespace cil::tools {

using CreateSig = IImageIO*();
using DestroyFn = void (*)(IImageIO*);

struct ImageIO::Impl {
  std::unique_ptr<dylib> lib;
  std::unique_ptr<IImageIO, DestroyFn> io{nullptr, nullptr};
};

ImageIO::ImageIO(std::string& error) : impl_(std::make_unique<Impl>()) {
  std::string lib_path;

  if (Unpacker unpacker;
      !unpacker.UnpackAsset(Unpacker::Asset::kImageIO, lib_path)) {
    error = "failed to unpack imageio shared library";
    LOG4CXX_ERROR(logger_tools, error);
    return;
  }

  LOG4CXX_INFO(logger_tools, "Loading imageio from: " << lib_path);

  try {
    impl_->lib =
        std::make_unique<dylib>(lib_path, dylib::no_filename_decorations);
  } catch (const std::exception& e) {
    error = std::string("failed to load imageio: ") + e.what();
    LOG4CXX_ERROR(logger_tools, error);
    return;
  }

  CreateSig* create_fn = nullptr;
  DestroyFn destroy_fn = nullptr;
  try {
    create_fn = impl_->lib->get_function<CreateSig>("create_imageio");
    destroy_fn = impl_->lib->get_function<void(IImageIO*)>("destroy_imageio");
  } catch (const std::exception& e) {
    error = std::string("failed to resolve imageio symbols: ") + e.what();
    LOG4CXX_ERROR(logger_tools, error);
    return;
  }

  IImageIO* raw = create_fn();
  if (!raw) {
    error = "create_imageio returned null";
    LOG4CXX_ERROR(logger_tools, error);
  } else {
    impl_->io = {raw, destroy_fn};
    LOG4CXX_INFO(logger_tools, "ImageIO loaded successfully");
  }
}

ImageIO::~ImageIO() = default;

bool ImageIO::SavePNG(const std::string& path, const uint8_t* data,
                      uint32_t width, uint32_t height,
                      uint32_t channels) const {
  if (!impl_->io) return false;
  return impl_->io->SavePNG(path, data, width, height, channels);
}

bool ImageIO::SaveJPG(const std::string& path, const uint8_t* data,
                      uint32_t width, uint32_t height, uint32_t channels,
                      int quality) const {
  if (!impl_->io) return false;
  return impl_->io->SaveJPG(path, data, width, height, channels, quality);
}

bool ImageIO::SaveBMP(const std::string& path, const uint8_t* data,
                      uint32_t width, uint32_t height,
                      uint32_t channels) const {
  if (!impl_->io) return false;
  return impl_->io->SaveBMP(path, data, width, height, channels);
}

}  // namespace cil::tools
