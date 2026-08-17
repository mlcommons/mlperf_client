#include "unpacker.h"

#include <log4cxx/logger.h>

#include <fstream>
#include <future>
#include <map>
#include <numeric>
#include <optional>
#include <thread>
#include <vector>

#include "assets/assets.h"
#include "execution_provider.h"
#include "minizip/mz.h"
#include "minizip/mz_strm.h"
#include "minizip/mz_strm_buf.h"
#include "minizip/mz_strm_split.h"
#include "minizip/mz_zip.h"
#include "minizip/mz_zip_rw.h"
#include "scope_exit.h"

#ifdef _WIN32
// clang-format off
#include <comdef.h>
#include <atlcomcli.h>
// clang-format on
#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>

#include <codecvt>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#undef GetCurrentDirectory

#elif MACOS

#include <TargetConditionals.h>
#include <sys/statvfs.h>

#endif

// utils.h includes Windows.h, which we want to include manually here to include
// some macros
#include "utils.h"

namespace fs = std::filesystem;
using namespace log4cxx;

LoggerPtr loggerUnpacker(Logger::getLogger("SystemController"));

namespace cil {
// Minimum allowed timeout for any zip operation (30 seconds)
constexpr std::chrono::milliseconds kMinTimeoutDuration{30 * 1000};
// Maximum allowed timeout to prevent indefinite operations (30 min)
constexpr std::chrono::milliseconds kMaxTimeoutDuration{30 * 60 * 1000};
// Conversion factor for calculating timeout from file size
// Each MB of file size adds 200ms to timeout
constexpr double kTimeoutPerMegabyte{200};
// Maximum number of entries allowed in a zip file
constexpr size_t kMaxMiniZipEntries{50000};

#ifdef _WIN32

std::optional<std::uint64_t> WinApiCalculateTotalUncompressedSize(
    const std::string& zip_file, const std::string& dest_dir) {
  fs::path zip_path = fs::absolute(std::filesystem::path(zip_file));

  std::wstring w_zip_path = zip_path.wstring();

  auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool com_initialized = SUCCEEDED(hr) && hr != S_FALSE;

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to initialize COM. Error code: " << hr);
    return std::nullopt;
  }

  std::uint64_t total_size = 0;

  IShellDispatch* pISD = nullptr;

  hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                        IID_IShellDispatch, (void**)&pISD);

  if (SUCCEEDED(hr) && pISD) {
    Folder* input_zip_file_ptr = nullptr;

    VARIANT v_file;
    VariantInit(&v_file);
    v_file.vt = VT_BSTR;
    v_file.bstrVal = SysAllocString(w_zip_path.c_str());

    hr = pISD->NameSpace(v_file, &input_zip_file_ptr);
    if (SUCCEEDED(hr) && input_zip_file_ptr) {
      FolderItems* p_items = nullptr;
      hr = input_zip_file_ptr->Items(&p_items);
      if (SUCCEEDED(hr) && p_items) {
        long item_count = 0;
        p_items->get_Count(&item_count);

        for (long i = 0; i < item_count; i++) {
          FolderItem* p_item = nullptr;
          VARIANT vIndex;
          VariantInit(&vIndex);
          vIndex.vt = VT_I4;
          vIndex.lVal = i;

          hr = p_items->Item(vIndex, &p_item);
          if (SUCCEEDED(hr) && p_item) {
            std::uint64_t file_size_64 = 0;
            if (CComQIPtr<FolderItem2> p_item2 = p_item; p_item2) {
              CComVariant varSize;
              hr =
                  p_item2->ExtendedProperty(CComBSTR(L"System.Size"), &varSize);
              if (SUCCEEDED(hr) && varSize.vt != VT_EMPTY) {
                varSize.ChangeType(VT_UI8);
                if (varSize.vt == VT_UI8) {
                  file_size_64 = varSize.ullVal;
                }
              }
            } else {
              LONG file_size_32 = 0;
              p_item->get_Size(&file_size_32);
              file_size_64 = static_cast<std::uint64_t>(file_size_32);
            }

            BSTR item_path_bstr = nullptr;
            p_item->get_Path(&item_path_bstr);
            if (item_path_bstr) {
              fs::path dest_file =
                  fs::path(dest_dir) /
                  fs::path(std::wstring(item_path_bstr)).filename();
              std::error_code ec;
              if (const auto size = fs::file_size(dest_file, ec);
                  ec || size != file_size_64) {
                total_size += file_size_64;
              }
              SysFreeString(item_path_bstr);
            } else {
              total_size += file_size_64;
            }

            p_item->Release();
          }
          VariantClear(&vIndex);
        }
        p_items->Release();
      }
      input_zip_file_ptr->Release();
    }

    VariantClear(&v_file);
    pISD->Release();
  } else {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to get IShellDispatch interface. Error code: " << hr);
  }
  if (com_initialized) CoUninitialize();

  return total_size;
}

std::vector<std::string> WinApiExtractZip(const std::string& zip_file,
                                          const std::string& dest_folder) {
  fs::path zip_path = fs::absolute(std::filesystem::path(zip_file));
  fs::path dest_path = fs::absolute(std::filesystem::path(dest_folder));

  std::wstring w_zip_path = zip_path.wstring();
  std::wstring w_dest_folder = dest_path.wstring();

  using cil::utils::ScopeExit;

  HRESULT hr;

  hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool com_initialized = SUCCEEDED(hr) && hr != S_FALSE;

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to initialize COM. Error code: " << hr);
    return {};
  }

  ScopeExit cleanup_com([&]() {
    if (com_initialized) CoUninitialize();
  });

  IShellDispatch* pISD = nullptr;

  hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                        IID_IShellDispatch, (void**)&pISD);

  if (FAILED(hr) || !pISD) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to get IShellDispatch interface. Error code: " << hr);
    return {};
  }

  ScopeExit cleanup_isd([&]() { pISD->Release(); });

  VARIANT v_dir, v_file, v_options, v_items;

  VariantInit(&v_dir);
  VariantInit(&v_file);
  VariantInit(&v_options);
  VariantInit(&v_items);

  ScopeExit cleanup_variant([&]() {
    VariantClear(&v_dir);
    VariantClear(&v_file);
    VariantClear(&v_options);
    VariantClear(&v_items);
  });

  v_dir.vt = VT_BSTR;
  v_dir.bstrVal = SysAllocString(w_dest_folder.c_str());

  v_file.vt = VT_BSTR;
  v_file.bstrVal = SysAllocString(w_zip_path.c_str());

  v_options.vt = VT_I4;
  v_options.lVal = FOF_NO_UI;

  Folder* folder_ptr = nullptr;
  Folder* input_zip_file_ptr = nullptr;

  pISD->NameSpace(v_dir, &folder_ptr);
  pISD->NameSpace(v_file, &input_zip_file_ptr);

  if (!folder_ptr || !input_zip_file_ptr) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to get Folder interface.");
    return {};
  }

  ScopeExit cleanup_folder([&input_zip_file_ptr, &folder_ptr]() {
    if (input_zip_file_ptr) input_zip_file_ptr->Release();
    if (folder_ptr) folder_ptr->Release();
  });

  FolderItems* p_items = nullptr;
  if (FAILED(input_zip_file_ptr->Items(&p_items) || !p_items)) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to get items from zip file");
    return {};
  }

  ScopeExit cleanup_items([&p_items]() {
    if (p_items) p_items->Release();
  });

  long item_count = 0;
  hr = p_items->get_Count(&item_count);

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to get item count from zip file");
    return {};
  }

  std::vector<std::string> extracted_files;
  for (long i = 0; i < item_count; i++) {
    FolderItem* p_item = nullptr;
    VARIANT vIndex;
    VariantInit(&vIndex);
    vIndex.vt = VT_I4;
    vIndex.lVal = i;

    if (auto hr = p_items->Item(vIndex, &p_item); FAILED(hr) || !p_item) break;

    BSTR item_path_bstr;
    p_item->get_Path(&item_path_bstr);
    fs::path extracted_file_path =
        fs::path(dest_folder) /
        fs::path(std::wstring(item_path_bstr)).filename();
    extracted_files.push_back(extracted_file_path.string());
    LOG4CXX_DEBUG(loggerUnpacker,
                  "Extracting file: " << extracted_file_path.string());
    p_item->Release();
    SysFreeString(item_path_bstr);

    VariantClear(&vIndex);
  }

  v_items.vt = VT_DISPATCH;
  v_items.pdispVal = p_items;
  folder_ptr->CopyHere(v_items, v_options);

  // ScopeExit objects will clean up the COM objects

  return extracted_files;
}
#endif

std::optional<std::uint64_t> MinizipCalculateTotalUncompressedSize(
    const std::string& zip_file, const std::string& dest_dir) {
  std::uint64_t total_size = 0;
  int32_t err = MZ_OK;
  void* reader = mz_zip_reader_create();
  if (!reader) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to create zip reader");
    return std::nullopt;
  }
  err = mz_zip_reader_open_file(reader, zip_file.c_str());
  if (err != MZ_OK) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to open zip file " << zip_file);
    mz_zip_reader_delete(&reader);
    return std::nullopt;
  }
  bool success = true;
  err = mz_zip_reader_goto_first_entry(reader);
  if (err == MZ_OK) {
    int num_entries = 0;
    do {
      num_entries++;
      if (num_entries > kMaxMiniZipEntries) {
        LOG4CXX_ERROR(loggerUnpacker, "Too many entries in zip file");
        success = false;
        break;
      }
      mz_zip_file* file_info = NULL;
      err = mz_zip_reader_entry_get_info(reader, &file_info);
      if (err != MZ_OK) {
        LOG4CXX_ERROR(loggerUnpacker, "Failed to get zip file info");
        success = false;
        break;
      }
      if (mz_zip_reader_entry_is_dir(reader) != MZ_OK) {
        fs::path dest_file = fs::path(dest_dir) / file_info->filename;
        std::error_code ec;
        if (const auto size = fs::file_size(dest_file, ec);
            ec || size != file_info->uncompressed_size) {
          total_size += file_info->uncompressed_size;
        }
      }
      err = mz_zip_reader_goto_next_entry(reader);
    } while (err == MZ_OK);
  }
  if (err != MZ_END_OF_LIST) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to read all entries from zip file");
    success = false;
  }
  return success ? std::optional<std::uint64_t>(total_size) : std::nullopt;
}

static fs::path ToExtendedPath(const fs::path& p) {
#ifdef _WIN32
  std::error_code ec;
  fs::path abs = fs::absolute(p, ec);
  if (ec) abs = p;
  abs = abs.lexically_normal().make_preferred();
  std::wstring w = abs.wstring();
  if (w.rfind(LR"(\\?\)", 0) == 0) return abs;
  if (w.rfind(LR"(\\)", 0) == 0)
    return fs::path(LR"(\\?\UNC\)" + w.substr(2));
  return fs::path(LR"(\\?\)" + w);
#else
  return p;
#endif
}

std::vector<std::string> UnZipUsingMinizip(const std::string& zip_file,
                                           const std::string& dest_dir,
                                           bool return_relative_paths) {
  std::vector<std::string> unzipped_files;
  int32_t err = MZ_OK;
  void* reader = mz_zip_reader_create();
  if (!reader) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to create zip reader");
    return unzipped_files;
  }
  err = mz_zip_reader_open_file(reader, zip_file.c_str());
  if (err != MZ_OK) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to open zip file " << zip_file);
    mz_zip_reader_delete(&reader);
    return unzipped_files;
  }
  err = mz_zip_reader_goto_first_entry(reader);
  if (err == MZ_OK) {
    int num_entries = 0;
    do {
      num_entries++;
      if (num_entries > kMaxMiniZipEntries) {
        LOG4CXX_ERROR(loggerUnpacker, "Too many entries in zip file");
        unzipped_files.clear();
        break;
      }
      mz_zip_file* file_info = NULL;
      err = mz_zip_reader_entry_get_info(reader, &file_info);
      if (err != MZ_OK) {
        LOG4CXX_ERROR(loggerUnpacker, "Failed to get zip file info");
        unzipped_files.clear();
        break;
      }
      std::string file_name = file_info->filename;
      fs::path dest_file = fs::path(dest_dir) / file_name;
      fs::path long_dest = ToExtendedPath(dest_file);
      if (mz_zip_reader_entry_is_dir(reader) == MZ_OK) {
        try {
          fs::create_directories(long_dest);
        } catch (const std::exception& e) {
          LOG4CXX_ERROR(loggerUnpacker, "Failed to create directory "
                                            << dest_file << " " << e.what());
          unzipped_files.clear();
          break;
        }
      } else {
        if (!fs::exists(long_dest.parent_path())) {
          try {
            fs::create_directories(long_dest.parent_path());
          } catch (const std::exception& e) {
            LOG4CXX_ERROR(loggerUnpacker, "Failed to create directory "
                                              << dest_file.parent_path() << " "
                                              << e.what());
            unzipped_files.clear();
            break;
          }
        }

        // get the file size from the zip archive

        bool skip_current_file = false;
        if (fs::exists(long_dest)) {
          std::error_code ec;
          if (const auto size = fs::file_size(long_dest, ec);
              !ec && size == file_info->uncompressed_size) {
            skip_current_file = true;
          }
        }

        if (!skip_current_file) {
          auto free_space =
              utils::GetAvailableDiskSpace(long_dest.parent_path().string());
          if (free_space < file_info->uncompressed_size) {
            std::stringstream error_ss{"Don't have enough space to unpack "};
            error_ss << file_name << ". Free space "
                     << std::to_string(free_space);
            error_ss << ", uncompressed file size "
                     << std::to_string(file_info->uncompressed_size);
            LOG4CXX_ERROR(loggerUnpacker, error_ss.str().c_str());
            unzipped_files.clear();
            break;
          }
          std::ofstream out_stream(long_dest, std::ios::binary);
          if (!out_stream.is_open()) {
            LOG4CXX_ERROR(loggerUnpacker, "Failed to open file " << dest_file);
            unzipped_files.clear();
            break;
          }
          if (mz_zip_reader_entry_open(reader) != MZ_OK) {
            LOG4CXX_ERROR(loggerUnpacker,
                          "Failed to open zip entry " << file_name);
            unzipped_files.clear();
            break;
          }
          char buffer[8192];
          uint32_t bytes_read = 0;
          uint64_t total_bytes_read = 0;
          while ((bytes_read = mz_zip_reader_entry_read(reader, buffer,
                                                        sizeof(buffer))) > 0) {
            out_stream.write(buffer, bytes_read);
            total_bytes_read += bytes_read;
          }
          out_stream.close();
          if (total_bytes_read != file_info->uncompressed_size) {
            LOG4CXX_ERROR(loggerUnpacker,
                          "Failed to read all data from zip entry, size "
                          "mismatch for file "
                              << file_name << " expected size: "
                              << file_info->uncompressed_size
                              << " actual size: " << total_bytes_read);
            unzipped_files.clear();
            break;
          }
        }
        unzipped_files.push_back(return_relative_paths ? file_name
                                                       : dest_file.string());
      }
      err = mz_zip_reader_goto_next_entry(reader);
    } while (err == MZ_OK);
  }
  if (err != MZ_END_OF_LIST) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to read all entries from zip file");
    unzipped_files.clear();
  }
  return unzipped_files;
}

fs::path CreateDepsDirectory(std::string temp = "") {
  fs::path deps_path;

  if (temp.empty()) {
    deps_path = utils::GetAppDefaultTempPath();
  } else {
    deps_path = fs::path(temp) / "MLPerf";
  }
  // check if the directory exists, if not create it
  bool created = false;
  if (!fs::exists(deps_path)) {
    try {
      created = fs::create_directories(deps_path);
    } catch (const std::exception& e) {
      // if we can't create the directory, use the current directory instead
      LOG4CXX_ERROR(loggerUnpacker, "Unable to create directory "
                                        << deps_path << " " << e.what()
                                        << " using temp directory instead");
      created = fs::create_directories(utils::GetAppDefaultTempPath());
      deps_path = utils::GetAppDefaultTempPath();
    }
    if (!created) {
      LOG4CXX_ERROR(loggerUnpacker, "Unable to create directory " << deps_path);
    }
  }
  return deps_path;
}

static const std::map<Unpacker::Asset, std::string>& GetAssetFileMapping() {
  using enum Unpacker::Asset;

  // An asset to file mapping for both Windows, MacOS and iOS

  static const std::map<Unpacker::Asset, std::string> file_map = {
      {kLog4cxxConfig, "Log4CxxConfig.xml"},
      {kConfigSchema, "ConfigSchema.json"},
      {kLLMInputFileSchema, "LLMInputSchema.json"},
      {kImageInputFileSchema, "ImageInputSchema.json"},
      {kOutputResultsSchema, "OutputResultsSchema.json"},
      {kDataVerificationFile, "data_verification.json"},
      {kDataVerificationFileSchema, "DataVerificationSchema.json"},
      {kConfigVerificationFile, "config_verification.json"},
      {kConfigExperimentalVerificationFile,
       "config_experimental_verification.json"},
      {kConfigExtendedVerificationFile, "config_extended_verification.json"},
      {kEPDependenciesConfig, "ep_dependencies_config.json"},
      {kEPDependenciesConfigSchema, "EPDependenciesConfigSchema.json"},
      {kVendorsDefault, "vendors_default.zip"},
#ifdef _WIN32
      {kNativeOpenVINO, "IHV_NativeOpenVINO.dll"},
      {kNativeQNN, "IHV_NativeQNN.dll"},
      {kOrtGenAI, "IHV_OrtGenAI.dll"},
      {kOrtGenAIRyzenAI, "IHV_OrtGenAI_RyzenAI.dll"},
      {kWindowsML, "IHV_WindowsML.dll"},
      {kGGML_EPs, "IHV_GGML_EPs.dll"},
      {kLlama_cpp_Vulkan, "llama_cpp_Vulkan.dll"},
      {kLlama_cpp_CUDA, "llama_cpp_CUDA.dll"},
      {kLlama_cpp_ROCm, "llama_cpp_ROCm.dll"},
      {kDiffusers, "IHV_Diffusers.dll"},
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_NVIDIA
      // Slash in Name → lands in nvidia/ subdir under deps_dir/Diffusers/
      // automatically (unpack_regular_file calls fs::create_directories
      // on the parent path before writing).
      {kDiffusers_NVIDIA, "nvidia/Diffusers_NVIDIA.dll"},
#endif
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_APPLE
      {kDiffusers_APPLE, "apple/Diffusers_APPLE.dll"},
#endif
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_AMD
      {kDiffusers_AMD, "amd/Diffusers_AMD.dll"},
#endif
      {kTokenizer, "tokenizer.dll"},
      {kImageIO, "imageio.dll"},
#elif __linux__
      {kNativeOpenVINO, "libIHV_NativeOpenVINO.so"},
      {kGGML_EPs, "libIHV_GGML_EPs.so"},
      {kLlama_cpp_Vulkan, "libllama_cpp_Vulkan.so"},
      {kLlama_cpp_CUDA, "libllama_cpp_CUDA.so"},
      {kTokenizer, "libtokenizer.so"},
      {kImageIO, "libimageio.so"},
#else  // Apple
      {kGGML_EPs, "libIHV_GGML_EPs.dylib"},
      {kLlama_cpp_Vulkan, "libllama_cpp_Vulkan.dylib"},
      {kLlama_cpp_Metal, "libllama_cpp_Metal.dylib"},
      {kMLX, "libIHV_MLX.dylib"},
      {kDiffusers, "libIHV_Diffusers.dylib"},
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_NVIDIA
      {kDiffusers_NVIDIA, "nvidia/libDiffusers_NVIDIA.dylib"},
#endif
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_APPLE
      {kDiffusers_APPLE, "apple/libDiffusers_APPLE.dylib"},
#endif
      {kTokenizer, "libtokenizer.dylib"},
      {kImageIO, "libimageio.dylib"},
#endif
  };
  return file_map;
}

static const std::map<Unpacker::Asset, std::tuple<const unsigned char**, int,
                                                  int, const unsigned*>>
GetAssetMemoryMapping() {
  using enum Unpacker::Asset;

  std::map<Unpacker::Asset,
           std::tuple<const unsigned char**, int, int, const unsigned*>>
      memory_map;

#define ADD_ASSET_MAP_ENTRY(file, file_name)                                  \
  memory_map.insert(                                                          \
      {file,                                                                  \
       {assets::file_name, assets::file_name##Size, assets::file_name##Parts, \
        assets::file_name##PartSizes}});

  ADD_ASSET_MAP_ENTRY(kLog4cxxConfig, Log4CxxConfig_xml);
  ADD_ASSET_MAP_ENTRY(kConfigSchema, ConfigSchema_json);
  ADD_ASSET_MAP_ENTRY(kLLMInputFileSchema, LLMInputSchema_json);
  ADD_ASSET_MAP_ENTRY(kImageInputFileSchema, ImageInputSchema_json);
  ADD_ASSET_MAP_ENTRY(kOutputResultsSchema, OutputResultsSchema_json);
  ADD_ASSET_MAP_ENTRY(kDataVerificationFile, data_verification_json);
  ADD_ASSET_MAP_ENTRY(kDataVerificationFileSchema, DataVerificationSchema_json);
  ADD_ASSET_MAP_ENTRY(kConfigVerificationFile, config_verification_json);
  ADD_ASSET_MAP_ENTRY(kConfigExperimentalVerificationFile,
                      config_experimental_verification_json);
  ADD_ASSET_MAP_ENTRY(kConfigExtendedVerificationFile,
                      config_extended_verification_json);
  ADD_ASSET_MAP_ENTRY(kEPDependenciesConfigSchema,
                      EPDependenciesConfigSchema_json);
#if VENDORS_DEFAULT_PACKED
  ADD_ASSET_MAP_ENTRY(kVendorsDefault, vendors_default_zip);
#endif

#ifdef _WIN32

#if !defined(_M_ARM64) && !defined(_M_ARM)
  ADD_ASSET_MAP_ENTRY(kEPDependenciesConfig,
                      ep_dependencies_config_windows_x64_json);
#else
  ADD_ASSET_MAP_ENTRY(kEPDependenciesConfig,
                      ep_dependencies_config_windows_ARM_json);
#endif

#if WITH_IHV_NATIVE_OPENVINO
  ADD_ASSET_MAP_ENTRY(kNativeOpenVINO, IHV_NativeOpenVINO_dll);
#endif  // WITH_IHV_NATIVE_OPENVINO

#if WITH_IHV_NATIVE_QNN
  ADD_ASSET_MAP_ENTRY(kNativeQNN, IHV_NativeQNN_dll);
#endif  // WITH_IHV_NATIVE_QNN

#if WITH_IHV_ORT_GENAI
  ADD_ASSET_MAP_ENTRY(kOrtGenAI, IHV_OrtGenAI_dll);
#endif
#if WITH_IHV_ORT_GENAI_RYZENAI
  ADD_ASSET_MAP_ENTRY(kOrtGenAIRyzenAI, IHV_OrtGenAI_RyzenAI_dll);
#endif
#if WITH_IHV_WIN_ML
  ADD_ASSET_MAP_ENTRY(kWindowsML, IHV_WindowsML_dll);
#endif

#if WITH_IHV_GGML
  ADD_ASSET_MAP_ENTRY(kGGML_EPs, IHV_GGML_EPs_dll);
#endif  // WITH_IHV_GGML

#if WITH_IHV_GGML_VULKAN
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_Vulkan, llama_cpp_Vulkan_dll);
#endif  // WITH_IHV_GGML_VULKAN

#if WITH_IHV_GGML_CUDA
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_CUDA, llama_cpp_CUDA_dll);
#endif  // WITH_IHV_GGML_CUDA

#if WITH_IHV_GGML_ROCM
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_ROCm, llama_cpp_ROCm_dll);
#endif  // WITH_IHV_GGML_ROCM

#if WITH_IHV_DIFFUSERS
  // In N=1 mode this blob IS the sub-EP renamed (built artifact
  // IHV_Diffusers.dll). In N≥2 mode it's the pure-C++ dispatcher
  // (built artifact IHV_Diffusers.dll, the dispatcher target).
  ADD_ASSET_MAP_ENTRY(kDiffusers, IHV_Diffusers_dll);
#if WITH_IHV_DIFFUSERS_DISPATCHER
  // Multi-vendor: one shared sub-EP blob (built artifact
  // Diffusers_Vendor.dll). Both vendor enums alias to the SAME bytes
  // — unpacker writes each into its own vendor subdir under different
  // file names (Diffusers_NVIDIA.dll, Diffusers_APPLE.dll, ...).
#if WITH_IHV_DIFFUSERS_NVIDIA
  ADD_ASSET_MAP_ENTRY(kDiffusers_NVIDIA, Diffusers_Vendor_dll);
#endif
#if WITH_IHV_DIFFUSERS_APPLE
  ADD_ASSET_MAP_ENTRY(kDiffusers_APPLE, Diffusers_Vendor_dll);
#endif
#if WITH_IHV_DIFFUSERS_AMD
  ADD_ASSET_MAP_ENTRY(kDiffusers_AMD, Diffusers_Vendor_dll);
#endif
#endif  // WITH_IHV_DIFFUSERS_DISPATCHER
#endif  // WITH_IHV_DIFFUSERS

  ADD_ASSET_MAP_ENTRY(kTokenizer, tokenizer_dll);
  ADD_ASSET_MAP_ENTRY(kImageIO, imageio_dll);

#elif __linux__ // _WIN32
   ADD_ASSET_MAP_ENTRY(kEPDependenciesConfig, ep_dependencies_config_linux_x64_json);

#if WITH_IHV_NATIVE_OPENVINO
  ADD_ASSET_MAP_ENTRY(kNativeOpenVINO, libIHV_NativeOpenVINO_so);
#endif  // WITH_IHV_NATIVE_OPENVINO
#if WITH_IHV_ORT_GENAI
  ADD_ASSET_MAP_ENTRY(kOrtGenAI, libIHV_OrtGenAI_so);
#endif  // WITH_IHV_ORT_GENAI
#if WITH_IHV_GGML
  ADD_ASSET_MAP_ENTRY(kGGML_EPs, libIHV_GGML_EPs_so);
#endif  // WITH_IHV_GGML

#if WITH_IHV_GGML_CUDA
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_CUDA, libllama_cpp_CUDA_so);
#endif  // WITH_IHV_GGML_CUDA
#if WITH_IHV_GGML_VULKAN
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_Vulkan, libllama_cpp_Vulkan_so);
#endif  // WITH_IHV_GGML_VULKAN

  ADD_ASSET_MAP_ENTRY(kTokenizer, libtokenizer_so);
  ADD_ASSET_MAP_ENTRY(kImageIO, libimageio_so);

#else  // __linux__

#if TARGET_OS_IOS
  ADD_ASSET_MAP_ENTRY(kEPDependenciesConfig, ep_dependencies_config_iOS_json);
#else
  ADD_ASSET_MAP_ENTRY(kEPDependenciesConfig, ep_dependencies_config_macOS_json);
#endif

#if WITH_IHV_GGML
  ADD_ASSET_MAP_ENTRY(kGGML_EPs, libIHV_GGML_EPs_dylib);
#endif

#if WITH_IHV_GGML_VULKAN
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_Vulkan, libllama_cpp_Vulkan_dylib);
#endif

#if WITH_IHV_GGML_METAL
  ADD_ASSET_MAP_ENTRY(kLlama_cpp_Metal, libllama_cpp_Metal_dylib);
#endif

#if WITH_IHV_MLX
  ADD_ASSET_MAP_ENTRY(kMLX, libIHV_MLX_dylib);
#endif

#if WITH_IHV_DIFFUSERS
  ADD_ASSET_MAP_ENTRY(kDiffusers, libIHV_Diffusers_dylib);
#endif  // WITH_IHV_DIFFUSERS

  ADD_ASSET_MAP_ENTRY(kTokenizer, libtokenizer_dylib);
  ADD_ASSET_MAP_ENTRY(kImageIO, libimageio_dylib);

#endif  // TARGET_OS_IOS

  return memory_map;
}

Unpacker::Unpacker() { deps_dir_ = CreateDepsDirectory(); }

Unpacker::~Unpacker() = default;

void Unpacker::SetDepsDir(const std::string& deps_dir, bool remove_old) {
  auto old_dir = deps_dir_;
  if (!deps_dir.empty()) deps_dir_ = CreateDepsDirectory(deps_dir);
  if (deps_dir_ != old_dir) {
    if (remove_old) {
      // move unpacked files from the old dir to the new dir
      for (const auto& path : unpacked_files_) {
        fs::path new_path = deps_dir_ / path.filename();
        fs::rename(path, new_path);
      }
      if (fs::is_empty(old_dir)) fs::remove(old_dir);
    } else {
      // copy to new directory
      for (const auto& path : unpacked_files_) {
        fs::path new_path = deps_dir_ / path.filename();
        if (!fs::exists(new_path)) {
          fs::copy(path, new_path);
        }
      }
    }
  }
}

std::string Unpacker::GetDepsDir() const { return deps_dir_.string(); }

bool Unpacker::UnpackAsset(Asset asset, std::string& path, bool forced) {
  using enum Asset;

  auto& file_map = GetAssetFileMapping();

  if (!file_map.contains(asset)) {
    LOG4CXX_ERROR(loggerUnpacker, "Unknown packed file type: " << (int)asset);
    return false;
  }

  auto file_name = file_map.at(asset);

  auto adjust_for_ep = [&](EP ep) {
    file_name = cil::GetEPDependencySubdirPath(ep) + "/" + file_name;
  };

  switch (asset) {
    case kNativeOpenVINO:
      adjust_for_ep(EP::kIHVNativeOpenVINO);
      break;
    case kNativeQNN:
      adjust_for_ep(EP::kIHVNativeQNN);
      break;
    case kOrtGenAI:
      adjust_for_ep(EP::kIHVOrtGenAI);
      break;
    case kOrtGenAIRyzenAI:
      adjust_for_ep(EP::kkIHVOrtGenAIRyzenAI);
      break;
    case kWindowsML:
      adjust_for_ep(EP::kIHVWindowsML);
      break;
    case kGGML_EPs:
    case kLlama_cpp_Vulkan:
    case kLlama_cpp_CUDA:
    case kLlama_cpp_Metal:
    case kLlama_cpp_ROCm:
      adjust_for_ep(EP::kIHVLlamaCpp);
      break;
    case kMLX:
      adjust_for_ep(EP::kIHVMLX);
      break;
    case kDiffusers:
#if WITH_IHV_DIFFUSERS_DISPATCHER
    case kDiffusers_NVIDIA:
    case kDiffusers_APPLE:
    case kDiffusers_AMD:
#endif
      adjust_for_ep(EP::kIHVDiffusers);
      break;
    case kTokenizer:
      file_name = "tokenizer/" + file_name;
      break;
    case kImageIO:
      file_name = "imageio/" + file_name;
      break;
  }

  auto unpack_regular_file = [&]() {
    fs::path dest_file = deps_dir_ / fs::path(file_name);
    path = dest_file.string();

    // We don't want to unpack the same file multiple times, so check if it
    // already exists in the unpacked files list

    if (!forced && std::find(unpacked_files_.begin(), unpacked_files_.end(),
                             dest_file.string()) != unpacked_files_.end()) {
      return true;
    }

    const auto& memory_map = GetAssetMemoryMapping();

    if (!memory_map.contains(asset)) {
      LOG4CXX_ERROR(loggerUnpacker, "Unknown packed file type: " << (int)asset);
      return false;
    }

    // Unpack the asset to IHV local directory inside deps directory
    fs::path dest_dir = dest_file.parent_path();
    if (!fs::exists(dest_dir)) {
      fs::create_directories(dest_dir);
    }

    const auto& [memory_ptr, file_size, file_parts, file_parts_size] =
        memory_map.at(asset);

    return ExtractFileFromMemory(memory_ptr, file_size, file_parts,
                                 file_parts_size, dest_file.string());
  };

  switch (asset) {
    case kLog4cxxConfig:
    case kConfigSchema:
    case kLLMInputFileSchema:
    case kImageInputFileSchema:
    case kOutputResultsSchema:
    case kDataVerificationFile:
    case kDataVerificationFileSchema:
    case kConfigVerificationFile:
    case kConfigExperimentalVerificationFile:
    case kConfigExtendedVerificationFile:
    case kEPDependenciesConfig:
    case kEPDependenciesConfigSchema:
#if VENDORS_DEFAULT_PACKED
    case kVendorsDefault:
#endif
#if (defined(_WIN32) || defined(linux) || defined(__linux__)) && \
    WITH_IHV_NATIVE_OPENVINO
    case kNativeOpenVINO:
#endif  // WITH_IHV_NATIVE_OPENVINO
#if (defined(_WIN32) || defined (__linux__)) && WITH_IHV_ORT_GENAI
    case kOrtGenAI:
#endif
#if defined(_WIN32) && WITH_IHV_ORT_GENAI_RYZENAI
    case kOrtGenAIRyzenAI:
#endif
#if defined(_WIN32) && WITH_IHV_WIN_ML
    case kWindowsML:
#endif
#if defined(_WIN32) && WITH_IHV_NATIVE_QNN
    case kNativeQNN:
#endif
#if WITH_IHV_GGML
    case kGGML_EPs:
#if WITH_IHV_GGML_VULKAN
    case kLlama_cpp_Vulkan:
#endif
#if (defined(_WIN32) || defined (__linux__)) && WITH_IHV_GGML_CUDA
    case kLlama_cpp_CUDA:
#endif
#if !defined(_WIN32) && WITH_IHV_GGML_METAL
    case kLlama_cpp_Metal:
#endif
#if defined(_WIN32) && WITH_IHV_GGML_ROCM
    case kLlama_cpp_ROCm:
#endif
#endif  // WITH_IHV_GGML
#if !defined(_WIN32) && WITH_IHV_MLX
    case kMLX:
#endif
#if WITH_IHV_DIFFUSERS
    case kDiffusers:
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_NVIDIA
    case kDiffusers_NVIDIA:
#endif
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_APPLE
    case kDiffusers_APPLE:
#endif
#if WITH_IHV_DIFFUSERS_DISPATCHER && WITH_IHV_DIFFUSERS_AMD
    case kDiffusers_AMD:
#endif
#endif
    case kTokenizer:
    case kImageIO:
      return unpack_regular_file();
    default:
      LOG4CXX_ERROR(loggerUnpacker, "Unknown packed file type: " << (int)asset);
      return false;
  }
}

static std::optional<std::uint64_t> CalculateTotalUncompressedSize(
    const std::string& zip_file, const std::string& dest_dir) {
  LOG4CXX_DEBUG(
      loggerUnpacker,
      "Calculating total uncompressed size of zip file: " << zip_file);
  auto total_uncompressed_size =
      MinizipCalculateTotalUncompressedSize(zip_file, dest_dir);
  if (!total_uncompressed_size.has_value()) {
    LOG4CXX_DEBUG(loggerUnpacker,
                  "Failed to calculate total uncompressed "
                  "size using minizip");
#ifdef _WIN32
    total_uncompressed_size =
        WinApiCalculateTotalUncompressedSize(zip_file, dest_dir);
    if (!total_uncompressed_size.has_value()) {
      LOG4CXX_DEBUG(loggerUnpacker,
                    "Failed to calculate total uncompressed "
                    "size using WinAPI");
    }
#endif
  }
  return total_uncompressed_size;
}

std::vector<std::string> Unpacker::UnpackFilesFromZIP(
    const std::string& zip_file, const std::string& dest_dir,
    bool return_relative_paths, int64_t timeout_ms) {
  if (!fs::exists(zip_file)) {
    LOG4CXX_ERROR(loggerUnpacker, "Zip file does not exist: " << zip_file);
    return {};
  }

  auto check_size = [&dest_dir,
                     &zip_file](std::uintmax_t total_uncompressed_size) {
    if (total_uncompressed_size == 0) return true;
    auto free_space = utils::GetAvailableDiskSpace(dest_dir);
    if (free_space >= total_uncompressed_size) return true;
    auto gb_needed = utils::DoubleToFixedString(
        utils::BytesToGb(total_uncompressed_size), 2);
    auto gb_available =
        utils::DoubleToFixedString(utils::BytesToGb(free_space), 2);
    LOG4CXX_ERROR(loggerUnpacker, "Not enough disk space to unpack "
                                      << zip_file << ": needs " << gb_needed
                                      << " GB, " << gb_available
                                      << " available");
    return false;
  };

  auto unzip = [zip_file, dest_dir, return_relative_paths]() {
    LOG4CXX_DEBUG(loggerUnpacker,
                  "Unpacking files from zip file: " << zip_file);
    std::vector<std::string> unzipped_files =
        UnZipUsingMinizip(zip_file, dest_dir, return_relative_paths);

    if (unzipped_files.empty()) {
      LOG4CXX_DEBUG(loggerUnpacker, "Failed to unzip  using minizip");
#ifdef _WIN32
      unzipped_files = WinApiExtractZip(zip_file, dest_dir);
      if (unzipped_files.empty()) {
        LOG4CXX_DEBUG(loggerUnpacker, "Failed to unzip using WinAPI");
      }
#endif
    }
    return unzipped_files;
  };

  try {
    std::uint64_t zip_size = 0;
    try {
      zip_size = std::filesystem::file_size(zip_file);
    } catch (const std::filesystem::filesystem_error& e) {
      LOG4CXX_ERROR(loggerUnpacker, "Failed to get file size of "
                                        << zip_file << ": " << e.what());
      return {};
    }
    if (zip_size == 0) {
      LOG4CXX_ERROR(loggerUnpacker, "Zip file is empty: " << zip_file);
      return {};
    }

    {
      auto promise =
          std::make_shared<std::promise<std::optional<std::uint64_t>>>();
      auto future = promise->get_future();
      std::thread worker([promise, zip_file, dest_dir]() {
        try {
          auto result = CalculateTotalUncompressedSize(zip_file, dest_dir);
          promise->set_value(result);
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });

      worker.detach();

      auto status =
          future.wait_for(timeout_ms > 0 ? std::chrono::milliseconds(timeout_ms)
                                         : kMaxTimeoutDuration);
      if (status != std::future_status::ready) {
        LOG4CXX_ERROR(loggerUnpacker,
                      "Calculating total uncompressed size of zip file: "
                          << zip_file << " timed out");
        return {};
      }

      auto uncompressed_size = future.get();
      zip_size = uncompressed_size.value_or(0);
      if (!uncompressed_size.has_value()) {
        LOG4CXX_WARN(loggerUnpacker,
                     "Could not calculate total uncompressed size for "
                         << zip_file
                         << ", disk space pre-check will be skipped");
      } else if (!check_size(*uncompressed_size)) {
        return {};
      }
    }

    if (timeout_ms == 0) return unzip();

    std::chrono::milliseconds timeout_duration;
    if (timeout_ms < 0) {
      const double file_size_mb =
          static_cast<double>(zip_size) / (1024.0 * 1024.0);
      timeout_duration = std::chrono::milliseconds(
          static_cast<int64_t>(file_size_mb * kTimeoutPerMegabyte));
      if (timeout_duration < kMinTimeoutDuration) {
        timeout_duration = kMinTimeoutDuration;
      } else if (timeout_duration > kMaxTimeoutDuration) {
        timeout_duration = kMaxTimeoutDuration;
      }
    } else if (timeout_ms > 0) {
      timeout_duration = std::chrono::milliseconds(timeout_ms);
    }

    {
      auto promise = std::make_shared<std::promise<std::vector<std::string>>>();
      auto future = promise->get_future();
      std::thread worker([promise, unzip]() {
        try {
          auto result = unzip();
          promise->set_value(result);
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });

      worker.detach();

      auto status = future.wait_for(timeout_duration);
      if (status != std::future_status::timeout) return future.get();
    }

    LOG4CXX_ERROR(loggerUnpacker, "Unpacking files from zip file: "
                                      << zip_file << " timed out");
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to unpack files from zip file: "
                                      << zip_file << " " << e.what());
  } catch (...) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to unpack files from zip file: " << zip_file);
  }
  return {};
}

bool Unpacker::ExtractFileFromMemory(const unsigned char* data[],
                                     size_t data_size, unsigned int parts,
                                     const unsigned part_sizes[],
                                     const std::string& file_path) {
  // A file already present at the embedded blob's exact size is byte-identical
  // (blob fixed per build, deps dir keyed by version) — skip the rewrite. Also
  // avoids failing when the file is a loaded, locked EP DLL.
  try {
    if (fs::exists(file_path) && fs::file_size(file_path) == data_size) {
      if (std::find(unpacked_files_.begin(), unpacked_files_.end(),
                    file_path) == unpacked_files_.end()) {
        unpacked_files_.emplace_back(file_path);
      }
      return true;
    }
  } catch (const fs::filesystem_error&) {
    // Fall through and attempt a normal extraction.
  }

#ifdef __APPLE__
  // dyld may fail to load an overwritten .dylib due to cached data, so remove
  // it first before writing.
  try {
    fs::path path_obj(file_path);
    if (fs::exists(path_obj) && path_obj.extension() == ".dylib") {
      if (!fs::remove(path_obj)) {
        LOG4CXX_ERROR(loggerUnpacker, "Failed to delete file " << file_path);
      }
    }
  } catch (const fs::filesystem_error& e) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to delete file " << file_path << " " << e.what());
  }
#endif  // __APPLE__

  std::ofstream out_file(file_path, std::ios::binary);
  size_t written = 0;
  bool success = false;
  if (out_file) {
    for (unsigned i = 0; i < parts; i++) {
      size_t chunk_size = part_sizes[i];
      out_file.write((const char*)data[i], chunk_size);
      written += chunk_size;
    }
    success = written == data_size;
    if (!success)
      LOG4CXX_ERROR(loggerUnpacker, "Unable to unpack file " << file_path);
    out_file.close();
  } else {
    LOG4CXX_ERROR(loggerUnpacker, "Unable to create file " << file_path);
  }
  if (success) unpacked_files_.emplace_back(file_path);
  return success;
}

bool Unpacker::ZipFiles(const std::string& zip_file,
                        const std::vector<std::string>& files_to_add) {
  void* writer = mz_zip_writer_create();
  if (!writer) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to create zip writer");
    return false;
  }

  int32_t err = mz_zip_writer_open_file(writer, zip_file.c_str(), 0, 0);
  if (err != MZ_OK) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to open zip file for writing " << zip_file);
    mz_zip_writer_delete(&writer);
    return false;
  }

  for (const std::string& file_path : files_to_add) {
    err = mz_zip_writer_add_path(writer, file_path.c_str(), NULL, 0, 0);
    if (err != MZ_OK) {
      LOG4CXX_ERROR(loggerUnpacker, "Failed to add file to zip " << file_path);
    }
  }

  mz_zip_writer_close(writer);
  mz_zip_writer_delete(&writer);

  return err == MZ_OK;
}

}  // namespace cil
