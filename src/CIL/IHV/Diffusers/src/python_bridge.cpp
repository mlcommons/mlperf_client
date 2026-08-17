#include "python_bridge.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string_view>

#include "inference_package_manifest.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;
namespace fs = std::filesystem;

namespace {
// The embedded interpreter is process-global and never finalised: torch/numpy
// C-extensions can't be re-imported after Py_Finalize.
std::mutex g_interpreter_mutex;
bool g_package_installed = false;
// Leaked on purpose: keeps the GIL released for the process lifetime.
py::gil_scoped_release* g_gil_release = nullptr;

// Anchor whose address identifies this dynamic library to dladdr().
void dylib_anchor() {}

// Pin this dylib: the harness unloads/reloads the EP, but the process-global
// interpreter stays wired into this library's pybind11 glue.
void PinSelfDylib() {
#ifdef _WIN32
  HMODULE self = nullptr;
  ::GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
      reinterpret_cast<LPCWSTR>(&dylib_anchor), &self);
#else
  Dl_info info;
  if (dladdr(reinterpret_cast<void*>(&dylib_anchor), &info) != 0 &&
      info.dli_fname != nullptr) {
    // RTLD_NODELETE + the leaked handle keep it resident through dlclose().
    void* h = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
    (void)h;
  }
#endif
}
}  // namespace

namespace cil {
namespace IHV {

PythonBridge::PythonBridge(const std::string& deps_dir, BridgeLogger logger)
    : deps_dir_(deps_dir), logger_(std::move(logger)) {}

PythonBridge::~PythonBridge() { ShutdownInterpreter(); }

fs::path PythonBridge::ResolvePythonHome(const std::string& deps_dir,
                                         const std::string& backend) {
  // Per-vendor python homes exist only in Windows multi-vendor (dispatcher)
  // builds. Apple is single-build (publishing embeds Python.framework, handled
  // by the caller), so MPS falls through to the plain deps_dir/python.
  std::string vendor_subdir;
  if (backend == "RYZENAI")
    vendor_subdir = "amd";
  else if (backend == "CUDA")
    vendor_subdir = "nvidia";

  if (!vendor_subdir.empty()) {
    const fs::path vendored = fs::path(deps_dir) / vendor_subdir / "python";
    if (fs::exists(vendored)) return vendored;
  }
  return fs::path(deps_dir) / "python";
}

bool PythonBridge::InitInterpreter(const std::string& backend) {
  std::lock_guard<std::mutex> lock(g_interpreter_mutex);

#ifdef _WIN32
  // set RyzenAI DoD base dir (DD_ROOT) before any Python import loads the DLL
  {
    std::string upper = backend;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "RYZENAI" &&
        GetEnvironmentVariableW(L"DD_ROOT", nullptr, 0) == 0) {
      const wchar_t* kRyzenaiDll = L"onnxruntime_providers_ryzenai.dll";
      fs::path base(deps_dir_);
      fs::path dd_root;
      if (fs::exists(base / kRyzenaiDll)) {
        dd_root = base;
      } else if (fs::exists(base.parent_path() / kRyzenaiDll)) {
        dd_root = base.parent_path();
      }
      if (!dd_root.empty()) {
        const std::wstring w = dd_root.wstring();
        _wputenv_s(L"DD_ROOT", w.c_str());
        SetEnvironmentVariableW(L"DD_ROOT", w.c_str());
        logger_(0, "RyzenAI DD_ROOT -> " + dd_root.string());
      }
    }
  }
#endif

  // The interpreter survives this dylib reloading; re-initialising aborts.
  if (Py_IsInitialized() != 0) {
    interpreter_started_ = true;
    return true;
  }

#ifndef _WIN32
  // torch's multiprocessing resource_tracker can re-exec and SIGPIPE the host.
  // The embedded module can't ignore it itself (signal.signal is
  // main-thread-only).
  std::signal(SIGPIPE, SIG_IGN);
#endif

  try {
    fs::path python_home = ResolvePythonHome(deps_dir_, backend);
    if (!fs::exists(python_home)) {
      error_message_ = "Bundled Python not found at: " + python_home.string();
      logger_(3, error_message_);
      return false;
    }

#ifdef _WIN32
    fs::path stdlib_dir = python_home / "Lib";
    fs::path dynload_dir = python_home / "DLLs";
    fs::path site_packages = python_home / "Lib" / "site-packages";
#else
    const std::string py_ver = "python" + std::to_string(PY_MAJOR_VERSION) +
                               "." + std::to_string(PY_MINOR_VERSION);
    fs::path stdlib_dir = python_home / "lib" / py_ver;
    fs::path dynload_dir = python_home / "lib" / py_ver / "lib-dynload";
    fs::path site_packages = python_home / "lib" / py_ver / "site-packages";
#endif

    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);

#ifdef _WIN32
    std::wstring home_w = python_home.wstring();
    PyConfig_SetString(&config, &config.home, home_w.c_str());
#else
    wchar_t* home_wc = Py_DecodeLocale(python_home.string().c_str(), nullptr);
    PyConfig_SetString(&config, &config.home, home_wc);
    PyMem_RawFree(home_wc);
#endif

    config.module_search_paths_set = 1;

    auto append_path = [&config](const fs::path& p) {
#ifdef _WIN32
      std::wstring ws = p.wstring();
      PyWideStringList_Append(&config.module_search_paths, ws.c_str());
#else
      wchar_t* wp = Py_DecodeLocale(p.string().c_str(), nullptr);
      PyWideStringList_Append(&config.module_search_paths, wp);
      PyMem_RawFree(wp);
#endif
    };

    append_path(stdlib_dir);
    if (fs::exists(dynload_dir)) append_path(dynload_dir);
    if (fs::exists(site_packages)) append_path(site_packages);
    append_path(fs::path(deps_dir_));

    py::initialize_interpreter(&config, 0, nullptr, false);
    PinSelfDylib();
    // Mark this as the C++-embedded interpreter (helpers.is_frozen_or_embedded).
    py::module_::import("sys").attr("_diffusers_embedded") = py::bool_(true);
    // gil_scoped_release (not raw PyEval_SaveThread) builds pybind11's
    // 'internals' while the GIL is held. Leaked on purpose.
    g_gil_release = new py::gil_scoped_release();
    interpreter_started_ = true;

    logger_(0, "Python interpreter initialized (isolated config)");
    return true;
  } catch (const py::error_already_set& e) {
    error_message_ = std::string("Python init failed: ") + e.what();
    logger_(3, error_message_);
    return false;
  } catch (const std::runtime_error& e) {
    error_message_ = std::string("Python init failed: ") + e.what();
    logger_(3, error_message_);
    return false;
  }
}

void PythonBridge::ShutdownInterpreter() {
  // Interpreter is never finalised (see above); only clear per-instance state.
  interpreter_started_ = false;
  initialized_ = false;
}

bool PythonBridge::EnsurePackageInstalled() {
  if (g_package_installed) return true;

  using namespace cil::IHV::embedded;

  const EmbeddedSource* bootstrap = nullptr;
  for (std::size_t i = 0; i < kEmbeddedSourcesCount; ++i) {
    if (std::string_view(kEmbeddedSources[i].name) == "_bootstrap") {
      bootstrap = &kEmbeddedSources[i];
      break;
    }
  }
  if (bootstrap == nullptr) {
    error_message_ = "Embedded _bootstrap module missing from manifest";
    logger_(3, error_message_);
    return false;
  }

  try {
    py::dict modules;
    for (std::size_t i = 0; i < kEmbeddedSourcesCount; ++i) {
      const auto& s = kEmbeddedSources[i];
      if (&s == bootstrap) continue;
      modules[py::str(s.name)] = py::bytes(s.source, s.length);
    }

    py::dict bootstrap_globals;
    py::exec(bootstrap->source, bootstrap_globals);
    bootstrap_globals["_install"](modules);

    g_package_installed = true;
    logger_(0, "Embedded inference package installed");
    return true;
  } catch (const py::error_already_set& e) {
    error_message_ =
        std::string("Embedded package install failed: ") + e.what();
    logger_(3, error_message_);
    return false;
  }
}

bool PythonBridge::LoadPipeline(
    const std::string& model_path, const std::string& backend, int device_id,
    const std::string& model_base_name, const std::string& gpu_mode,
    const std::string& quantization,
    const std::map<std::string, std::string>& component_files,
    const std::string& pipeline_class, const std::string& hf_base_id,
    const std::string& optimizations_json) {
  if (!InitInterpreter(backend)) return false;
  py::gil_scoped_acquire gil;
  if (!EnsurePackageInstalled()) return false;

  try {
    py::module_ mod = py::module_::import("diffusers_inference");
    py::object pipe = mod.attr("ImageInference")();
    pipe.attr("load")(model_path, backend, device_id, model_base_name, gpu_mode,
                      quantization, component_files, pipeline_class, hf_base_id,
                      optimizations_json);

    py::module_ main = py::module_::import("__main__");
    main.attr("_pipeline") = pipe;

    initialized_ = true;
    logger_(0, "Diffusers pipeline loaded");
    return true;
  } catch (const py::error_already_set& e) {
    error_message_ = std::string("LoadPipeline failed: ") + e.what();
    logger_(3, error_message_);
    return false;
  }
}

bool PythonBridge::EnumerateDevices(const std::string& backend,
                                    std::vector<DeviceInfo>& out) {
  if (!InitInterpreter(backend)) return false;
  py::gil_scoped_acquire gil;
  if (!EnsurePackageInstalled()) return false;

  try {
    py::module_ probe = py::module_::import("hardware_probe");
    py::list devices = probe.attr("enumerate_devices")(backend);

    out.clear();
    for (auto&& item : devices) {
      auto d = py::reinterpret_borrow<py::dict>(item);
      DeviceInfo info;
      info.device_id = d["device_id"].cast<int>();
      info.name = d["device_name"].cast<std::string>();
      if (d.contains("total_memory_gb")) {
        info.total_memory_gb = d["total_memory_gb"].cast<float>();
      }
      out.push_back(std::move(info));
    }
    return true;
  } catch (const py::error_already_set& e) {
    error_message_ = std::string("EnumerateDevices failed: ") + e.what();
    logger_(3, error_message_);
    return false;
  }
}

PythonBridge::ImageResult PythonBridge::GenerateImage(
    const std::string& prompt, const std::string& negative_prompt,
    uint32_t width, uint32_t height, uint32_t num_inference_steps,
    float guidance_scale, int seed, uint32_t num_images_per_prompt,
    const std::vector<int>& seeds,
    std::function<void(unsigned, unsigned)> step_callback,
    std::function<void(const std::string&, double)> submodule_callback) {
  ImageResult result;

  if (!initialized_) {
    error_message_ = "Pipeline not initialized";
    return result;
  }

  py::gil_scoped_acquire gil;
  try {
    py::module_ main = py::module_::import("__main__");
    py::object pipe = main.attr("_pipeline");

    py::object py_callback = py::none();
    if (step_callback) {
      py_callback =
          py::cpp_function([step_callback](unsigned step, unsigned total) {
            step_callback(step, total);
          });
    }

    py::object py_submodule_cb = py::none();
    if (submodule_callback) {
      py_submodule_cb = py::cpp_function(
          [submodule_callback](const std::string& name, double ms) {
            submodule_callback(name, ms);
          });
    }

    py::object image_np = pipe.attr("generate")(
        prompt, negative_prompt, width, height, num_inference_steps,
        guidance_scale, seed, num_images_per_prompt, seeds, py_callback,
        py_submodule_cb);

    auto buf = image_np.cast<py::array_t<uint8_t>>();
    auto info = buf.request();

    size_t total_bytes = static_cast<size_t>(info.size);
    result.pixel_data.resize(total_bytes);
    std::memcpy(result.pixel_data.data(), info.ptr, total_bytes);
    result.width = width;
    result.height = height;

    return result;
  } catch (const py::error_already_set& e) {
    error_message_ = std::string("GenerateImage failed: ") + e.what();
    logger_(3, error_message_);
    return result;
  }
}

void PythonBridge::Unload() {
  if (!interpreter_started_) return;

  py::gil_scoped_acquire gil;
  try {
    py::module_ main = py::module_::import("__main__");
    if (py::hasattr(main, "_pipeline")) {
      py::object pipe = main.attr("_pipeline");
      pipe.attr("unload")();
      main.attr("_pipeline") = py::none();
    }
    initialized_ = false;
    logger_(0, "Pipeline unloaded");
  } catch (const py::error_already_set& e) {
    logger_(2, std::string("Unload warning: ") + e.what());
  }
}

}  // namespace IHV
}  // namespace cil
