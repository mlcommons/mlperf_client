#include "diffusers_dispatcher.h"

#include <filesystem>
#include <string>
#include <string_view>

#include "dylib.hpp"
#include "nlohmann/json.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#include <dlfcn.h>
#endif

#define API_IHV_DIFFUSERS_DISPATCHER_NAME "Diffusers_Dispatcher"
#define API_IHV_DIFFUSERS_DISPATCHER_VERSION "0.1.0"

namespace cil {
namespace IHV {

namespace {

// Directory of this dispatcher DLL — the anchor for locating EP libraries.
std::string GetCurrentLibraryDirectory() {
#ifdef _WIN32
  HMODULE hMod = nullptr;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(&GetCurrentLibraryDirectory),
                          &hMod)) {
    return {};
  }
  char buffer[MAX_PATH];
  const DWORD len = GetModuleFileNameA(hMod, buffer, MAX_PATH);
  if (len == 0) return {};
  return std::filesystem::path(std::string(buffer, len)).parent_path().string();
#else
  Dl_info dl_info{};
  if (dladdr(reinterpret_cast<void*>(&GetCurrentLibraryDirectory), &dl_info) &&
      dl_info.dli_fname != nullptr) {
    return std::filesystem::path(dl_info.dli_fname).parent_path().string();
  }
  return {};
#endif
}

// Uppercase vendor token from EP settings: explicit `device_vendor`, else
// inferred from `backend` (CUDA→NVIDIA, MPS→APPLE, XPU→INTEL). Empty if none.
std::string ResolveVendor(const std::string& ep_settings_json) {
  try {
    const auto j = nlohmann::json::parse(ep_settings_json);
    if (auto it = j.find("device_vendor"); it != j.end() && it->is_string()) {
      std::string v = it->get<std::string>();
      for (auto& c : v) c = static_cast<char>(::toupper(c));
      if (!v.empty()) return v;
    }
    if (auto it = j.find("backend"); it != j.end() && it->is_string()) {
      const std::string b = it->get<std::string>();
      if (b == "CUDA") return "NVIDIA";
      if (b == "MPS") return "APPLE";
      if (b == "XPU") return "INTEL";
      if (b == "RYZENAI") return "AMD";
    }
  } catch (const std::exception&) {
    // Fall through — caller will report the empty vendor.
  }
  return {};
}

// EP-library path: <dispatcher_dir>/<vendor_lc>/[lib]Diffusers_<VENDOR>.<ext>.
std::string BuildEpLibraryPath(const std::string& dispatcher_dir,
                               const std::string& vendor_upper) {
  std::string vendor_lower = vendor_upper;
  for (auto& c : vendor_lower) c = static_cast<char>(::tolower(c));

#ifdef _WIN32
  return dispatcher_dir + "/" + vendor_lower + "/Diffusers_" + vendor_upper +
         ".dll";
#elif TARGET_OS_IOS
  // iOS framework layout: libraries live as bundles next to the parent.
  return std::filesystem::path(dispatcher_dir).parent_path().string() + "/" +
         vendor_lower + "/Diffusers_" + vendor_upper + ".framework/Diffusers_" +
         vendor_upper;
#else
  return dispatcher_dir + "/" + vendor_lower + "/libDiffusers_" + vendor_upper +
         ".dylib";
#endif
}

}  // namespace

Diffusers_Dispatcher::~Diffusers_Dispatcher() { library_.reset(); }

const API_IHV_Struct_t* Diffusers_Dispatcher::Setup(
    const API_IHV_Setup_t* api) {
  const std::string vendor = ResolveVendor(api->ep_settings);
  if (vendor.empty()) {
    api->logger(api->context, API_IHV_LogLevel::API_IHV_FATAL,
                "Diffusers dispatcher: no supported vendor found in "
                "ep_settings (expected backend=CUDA/MPS/XPU or explicit "
                "device_vendor=NVIDIA/APPLE/INTEL).");
    return nullptr;
  }

  const std::string dispatcher_dir = GetCurrentLibraryDirectory();
  if (dispatcher_dir.empty()) {
    api->logger(
        api->context, API_IHV_LogLevel::API_IHV_FATAL,
        "Diffusers dispatcher: failed to resolve own library directory.");
    return nullptr;
  }

  const std::string ep_library_path =
      BuildEpLibraryPath(dispatcher_dir, vendor);

  try {
    library_ = std::make_unique<dylib>(ep_library_path.c_str(),
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
  } catch (const std::exception& ex) {
    library_.reset();
    const std::string msg =
        "Diffusers dispatcher: failed to load EP library at " +
        ep_library_path + " (vendor=" + vendor + "): " + ex.what();
    api->logger(api->context, API_IHV_LogLevel::API_IHV_FATAL, msg.c_str());
    return nullptr;
  }

  ihv_struct_ = setup_(api);
  if (!ihv_struct_) return nullptr;

  // Present the dispatcher as the IHV the harness sees, passing through the
  // EP library's device_type. ihv_struct_ keeps the EP library's ihv_data
  // reachable.
  auto* overridden = new API_IHV_Struct_t{
      API_IHV_DIFFUSERS_DISPATCHER_NAME, ihv_struct_->device_type,
      API_IHV_DIFFUSERS_DISPATCHER_VERSION, this, API_IHV_VERSION};
  return overridden;
}

bool Diffusers_Dispatcher::EnumerateDevices(
    struct API_IHV_DeviceEnumeration_t* api) {
  API_IHV_DeviceEnumeration_t overridden{api->context, api->logger, ihv_struct_,
                                         api->device_list};
  const bool ret = enumerate_(&overridden);
  api->device_list = overridden.device_list;
  return ret;
}

int Diffusers_Dispatcher::Init(const API_IHV_Init_t* api) {
  API_IHV_Init_t overridden{api->context, api->logger, ihv_struct_,
                            api->model_config};
  return init_(&overridden);
}

int Diffusers_Dispatcher::Prepare(const API_IHV_Simple_t* api) {
  API_IHV_Simple_t overridden{api->context, api->logger, ihv_struct_};
  return prepare_(&overridden);
}

int Diffusers_Dispatcher::Infer(API_IHV_Infer_t* api) {
  API_IHV_Infer_t overridden{api->context, api->logger, ihv_struct_,
                             api->io_data};
  return infer_(&overridden);
}

int Diffusers_Dispatcher::Reset(const API_IHV_Simple_t* api) {
  API_IHV_Simple_t overridden{api->context, api->logger, ihv_struct_};
  return reset_(&overridden);
}

int Diffusers_Dispatcher::Deinit(const API_IHV_Deinit_t* api) {
  API_IHV_Deinit_t overridden{api->context, api->logger, ihv_struct_};
  return deinit_(&overridden);
}

void Diffusers_Dispatcher::Release(const API_IHV_Release_t* api) {
  API_IHV_Release_t overridden{api->context, api->logger, ihv_struct_};
  release_(&overridden);
}

}  // namespace IHV
}  // namespace cil

unsigned API_IHV_GetAPIVersion(void) { return API_IHV_VERSION; }

const API_IHV_Struct_t* API_IHV_Setup(const API_IHV_Setup_t* api) {
  auto* handler = new cil::IHV::Diffusers_Dispatcher();
  const auto* ihv_struct = handler->Setup(api);
  if (!ihv_struct) delete handler;
  return ihv_struct;
}

int API_IHV_EnumerateDevices(struct API_IHV_DeviceEnumeration_t* api) {
  return static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data)
      ->EnumerateDevices(api);
}

int API_IHV_Init(const API_IHV_Init_t* api) {
  return static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data)
      ->Init(api);
}

int API_IHV_Prepare(const API_IHV_Simple_t* api) {
  return static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data)
      ->Prepare(api);
}

int API_IHV_Infer(API_IHV_Infer_t* api) {
  return static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data)
      ->Infer(api);
}

int API_IHV_Reset(const API_IHV_Simple_t* api) {
  return static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data)
      ->Reset(api);
}

int API_IHV_Deinit(const API_IHV_Deinit_t* api) {
  return static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data)
      ->Deinit(api);
}

void API_IHV_Release(const API_IHV_Release_t* api) {
  if (api->ihv_struct == nullptr) return;
  auto* handler =
      static_cast<cil::IHV::Diffusers_Dispatcher*>(api->ihv_struct->ihv_data);
  handler->Release(api);
  delete handler;
}
