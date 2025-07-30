#include "ggml_eps_handler.h"

#include <filesystem>

#include "dylib.hpp"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define API_IHV_GGML_EPS_NAME "GGML_EPs_Handler"
#define API_IHV_GGML_EPS_VERSION "0.0.1"

namespace cil {
namespace IHV {

#ifdef __APPLE__
static std::string GetCurrentDylibDirectory() {
  Dl_info dl_info;
  if (dladdr((void*)&GetCurrentDylibDirectory, &dl_info) && dl_info.dli_fname) {
    return std::filesystem::path(dl_info.dli_fname).parent_path();
  } else {
    return "";
  }
}
#endif

GGML_EPs_Handler::~GGML_EPs_Handler() { library_.reset(); }

const API_IHV_Struct_t* GGML_EPs_Handler::Setup(const API_IHV_Setup_t* api) {
  std::string ep_name = api->ep_name;
  if (ep_name.find("Vulkan") == std::string::npos &&
      ep_name.find("CUDA") == std::string::npos &&
      ep_name.find("Metal") == std::string::npos) {
    api->logger(api->context, API_IHV_LogLevel::API_IHV_FATAL,
                ("EP " + ep_name + " is not supported").c_str());
    return nullptr;
  }
#ifdef __APPLE__
  std::string current_dylib_dir = GetCurrentDylibDirectory();
#endif
  std::string library_name = "GGML_Metal";
  if (ep_name.find("Vulkan") != std::string::npos) {
    library_name = "GGML_Vulkan";
#ifdef __APPLE__
    setenv("VK_ICD_FILENAMES",
           (current_dylib_dir + "/MoltenVK_icd.json").c_str(), 1);
#endif
  } else if (ep_name.find("CUDA") != std::string::npos) {
    library_name = "GGML_CUDA";
  }
#ifdef _WIN32
  library_name += ".dll";
#elif TARGET_OS_IOS
  library_name =
      std::filesystem::path(current_dylib_dir).parent_path().string() + "/" +
      library_name + ".framework/" + library_name;
#else
  library_name = current_dylib_dir + "/lib" + library_name + ".dylib";
#endif

  try {
    library_ = std::make_unique<dylib>(library_name.c_str(),
                                       dylib::no_filename_decorations);
    setup_ = library_->get_function<std::remove_pointer_t<API_IHV_Setup_func>>(
        "API_IHV_Setup");
    enumerate_ = library_->get_function<
        std::remove_pointer_t<API_IHV_EnumerateDevices_func>>(
        "API_IHV_EnumerateDevices");
    init_ = library_->get_function<std::remove_pointer_t<API_IHV_Init_func>>(
        "API_IHV_Init");
    prepare_ =
        library_->get_function<std::remove_pointer_t<API_IHV_Prepare_func>>(
            "API_IHV_Prepare");
    infer_ = library_->get_function<std::remove_pointer_t<API_IHV_Infer_func>>(
        "API_IHV_Infer");
    reset_ = library_->get_function<std::remove_pointer_t<API_IHV_Reset_func>>(
        "API_IHV_Reset");
    deinit_ =
        library_->get_function<std::remove_pointer_t<API_IHV_Deinit_func>>(
            "API_IHV_Deinit");
    release_ =
        library_->get_function<std::remove_pointer_t<API_IHV_Release_func>>(
            "API_IHV_Release");
  } catch (std::exception& ex) {
    library_.reset();  // in case it was loaded
    api->logger(api->context, API_IHV_LogLevel::API_IHV_FATAL, ex.what());
  }

  ihv_struct_ = setup_(api);
  if (!ihv_struct_) return nullptr;

  auto ihv_struct_overriden =
      new API_IHV_Struct_t{API_IHV_GGML_EPS_NAME, ihv_struct_->device_type,
                           API_IHV_GGML_EPS_VERSION, this};
  return ihv_struct_overriden;
}

bool GGML_EPs_Handler::EnumerateDevices(
    struct API_IHV_DeviceEnumeration_t* api) {
  API_IHV_DeviceEnumeration_t overridden{api->context, api->logger, ihv_struct_,
                                         api->device_list};
  bool ret = enumerate_(&overridden);
  api->device_list = overridden.device_list;
  return ret;
}

int GGML_EPs_Handler::Init(const API_IHV_Init_t* api) {
  API_IHV_Init_t overridden{api->context, api->logger, ihv_struct_,
                            api->model_config};
  return init_(&overridden);
}

int GGML_EPs_Handler::Prepare(const API_IHV_Simple_t* api) {
  API_IHV_Simple_t overridden{api->context, api->logger, ihv_struct_};
  return prepare_(&overridden);
}

int GGML_EPs_Handler::Infer(API_IHV_Infer_t* api) {
  API_IHV_Infer_t overridden{api->context, api->logger, ihv_struct_,
                             api->io_data};
  return infer_(&overridden);
}

int GGML_EPs_Handler::Reset(const API_IHV_Simple_t* api) {
  API_IHV_Simple_t overridden{api->context, api->logger, ihv_struct_};
  return reset_(&overridden);
}

int GGML_EPs_Handler::Deinit(const API_IHV_Deinit_t* api) {
  API_IHV_Deinit_t overridden{api->context, api->logger, ihv_struct_};
  return deinit_(&overridden);
}

void GGML_EPs_Handler::Release(const API_IHV_Release_t* api) {
  API_IHV_Release_t overridden{api->context, api->logger, ihv_struct_};
  release_(&overridden);
}

}  // namespace IHV
}  // namespace cil

const API_IHV_Struct_t* API_IHV_Setup(const API_IHV_Setup_t* api) {
  auto handler = new cil::IHV::GGML_EPs_Handler();
  auto ihv_struct = handler->Setup(api);
  if (!ihv_struct) delete handler;
  return ihv_struct;
}

int API_IHV_EnumerateDevices(struct API_IHV_DeviceEnumeration_t* api) {
  return static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data)
      ->EnumerateDevices(api);
}

int API_IHV_Init(const API_IHV_Init_t* api) {
  return static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data)
      ->Init(api);
}

int API_IHV_Prepare(const API_IHV_Simple_t* api) {
  return static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data)
      ->Prepare(api);
}

int API_IHV_Infer(API_IHV_Infer_t* api) {
  return static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data)
      ->Infer(api);
}

int API_IHV_Reset(const API_IHV_Simple_t* api) {
  return static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data)
      ->Reset(api);
}

int API_IHV_Deinit(const API_IHV_Deinit_t* api) {
  return static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data)
      ->Deinit(api);
}

void API_IHV_Release(const API_IHV_Release_t* api) {
  if (api->ihv_struct == nullptr) {
    return;
  }
  auto handler =
      static_cast<cil::IHV::GGML_EPs_Handler*>(api->ihv_struct->ihv_data);
  handler->Release(api);
  delete handler;
}
