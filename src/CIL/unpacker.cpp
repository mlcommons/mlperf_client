#include "unpacker.h"

#include <log4cxx/logger.h>

#include <fstream>
#include <future>
#include <map>
#include <numeric>
#include <thread>
#include <vector>

#include "scope_exit.h"

#include "assets/assets.h"
#include "execution_provider.h"
#include "minizip/mz.h"
#include "minizip/mz_strm.h"
#include "minizip/mz_strm_buf.h"
#include "minizip/mz_strm_split.h"
#include "minizip/mz_zip.h"
#include "minizip/mz_zip_rw.h"

// Include Windows headers if available
#ifdef _WIN32
#include <comdef.h>
#include <atlcomcli.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>

#include <codecvt>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#undef GetCurrentDirectory
#else
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
constexpr size_t kMaxMiniZipEntries{10000};

#ifdef _WIN32

std::uint64_t WinApiCalculateTotalUncompressedSize(
    const std::string& zip_file) {
  fs::path zip_path = fs::absolute(std::filesystem::path(zip_file));

  std::wstring w_zip_path = zip_path.wstring();

  auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool com_initialized = SUCCEEDED(hr) && hr != S_FALSE;

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerUnpacker,
                  "Failed to initialize COM. Error code: " << hr);
    return 0;
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
            if (CComQIPtr<FolderItem2> p_item2 = p_item; p_item2) {
              CComVariant varSize;
              hr =
                  p_item2->ExtendedProperty(CComBSTR(L"System.Size"), &varSize);
              if (SUCCEEDED(hr) && varSize.vt != VT_EMPTY) {
                varSize.ChangeType(VT_UI8);
                if (varSize.vt == VT_UI8) {
                  std::uint64_t file_size_64 = varSize.ullVal;
                  total_size += file_size_64;
                }
              }
            } else {
              LONG file_size_32 = 0;
              p_item->get_Size(&file_size_32);
              total_size += (std::uint64_t)file_size_32;
            }
            p_item->Release();
          }
          VariantClear(&vIndex);
        }
        p_items->Release();
      }
      input_zip_file_ptr->Release();
    }

    SysFreeString(v_file.bstrVal);
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

    auto hr = p_items->Item(vIndex, &p_item);

    if (FAILED(hr) || !p_item) break;

    BSTR item_name;
    p_item->get_Name(&item_name);
    std::wstring wstr(item_name);
    fs::path extracted_file_path = fs::path(dest_folder) / fs::path(wstr);
    extracted_files.push_back(extracted_file_path.string());
    LOG4CXX_DEBUG(loggerUnpacker,
                  "Extracting file: " << extracted_file_path.string());
    p_item->Release();
    SysFreeString(item_name);

    VariantClear(&vIndex);
  }

  v_items.vt = VT_DISPATCH;
  v_items.pdispVal = p_items;
  folder_ptr->CopyHere(v_items, v_options);

  // ScopeExit objects will clean up the COM objects

  return extracted_files;
}
#endif

std::uint64_t MinizipCalculateTotalUncompressedSize(
    const std::string& zip_file) {
  std::uint64_t total_size = 0;
  int32_t err = MZ_OK;
  void* reader = mz_zip_reader_create();
  if (!reader) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to create zip reader");
    return total_size;
  }
  err = mz_zip_reader_open_file(reader, zip_file.c_str());
  if (err != MZ_OK) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to open zip file " << zip_file);
    mz_zip_reader_delete(&reader);
    return total_size;
  }
  err = mz_zip_reader_goto_first_entry(reader);
  if (err == MZ_OK) {
    int num_entries = 0;
    do {
      num_entries++;
      if (num_entries > kMaxMiniZipEntries) {
        LOG4CXX_ERROR(loggerUnpacker, "Too many entries in zip file");
        total_size = 0;
        break;
      }
      mz_zip_file* file_info = NULL;
      err = mz_zip_reader_entry_get_info(reader, &file_info);
      if (err != MZ_OK) {
        LOG4CXX_ERROR(loggerUnpacker, "Failed to get zip file info");
        total_size = 0;
        break;
      }
      std::string file_name = file_info->filename;
      if (mz_zip_reader_entry_is_dir(reader) != MZ_OK) {
        // get the file size from the zip archive

        total_size += file_info->uncompressed_size;
      }
      err = mz_zip_reader_goto_next_entry(reader);
    } while (err == MZ_OK);
  }
  if (err != MZ_END_OF_LIST) {
    LOG4CXX_ERROR(loggerUnpacker, "Failed to read all entries from zip file");
    total_size = 0;
  }
  return total_size;
}

std::vector<std::string> UnZipUsingMinizip(const std::string& zip_file,
                                           const std::string& dest_dir) {
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
      if (mz_zip_reader_entry_is_dir(reader) == MZ_OK) {
        try {
          fs::create_directories(dest_file);
        } catch (const std::exception& e) {
          LOG4CXX_ERROR(loggerUnpacker, "Failed to create directory "
                                            << dest_file << " " << e.what());
          unzipped_files.clear();
          break;
        }
      } else {
        if (!fs::exists(dest_file.parent_path())) {
          try {
            fs::create_directories(dest_file.parent_path());
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
        if (fs::exists(dest_file)) {
          std::error_code ec;
          std::uintmax_t size = fs::file_size(dest_file, ec);
          if (!ec && size == file_info->uncompressed_size) {
            skip_current_file = true;
          }
        }
        auto free_space =
            utils::GetAvailableDiskSpace(dest_file.parent_path().string());
        if (free_space < file_info->uncompressed_size) {
          std::stringstream error_ss{"Don't have enough space to unpack "};
          error_ss << file_name << ". Free space "
                   << std::to_string(free_space);
          error_ss << ", uncopressed file size "
                   << std::to_string(file_info->uncompressed_size);
          auto error_string = error_ss.str();
          LOG4CXX_ERROR(loggerUnpacker, error_string.c_str());
          skip_current_file = true;
        }

        if (!skip_current_file) {
          std::ofstream out_stream(dest_file, std::ios::binary);
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
        unzipped_files.push_back(dest_file.string());
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
      {kLlama2InputFileSchema, "Llama2InputSchema.json"},
      {kOutputResultsSchema, "OutputResultsSchema.json"},
      {kDataVerificationFile, "data_verification.json"},
      {kDataVerificationFileSchema, "DataVerificationSchema.json"},
      {kConfigVerificationFile, "config_verification.json"},
      {kEPDependenciesConfig, "ep_dependencies_config.json"},
      {kEPDependenciesConfigSchema, "EPDependenciesConfigSchema.json"},
#ifdef _WIN32
      {kNativeOpenVINO, "IHV_NativeOpenVINO.dll"},
      {kOrtGenAI, "IHV_OrtGenAI.dll"},
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
  ADD_ASSET_MAP_ENTRY(kLlama2InputFileSchema, Llama2InputSchema_json);
  ADD_ASSET_MAP_ENTRY(kOutputResultsSchema, OutputResultsSchema_json);
  ADD_ASSET_MAP_ENTRY(kDataVerificationFile, data_verification_json);
  ADD_ASSET_MAP_ENTRY(kDataVerificationFileSchema, DataVerificationSchema_json);
  ADD_ASSET_MAP_ENTRY(kConfigVerificationFile, config_verification_json);
  ADD_ASSET_MAP_ENTRY(kEPDependenciesConfigSchema,
                      EPDependenciesConfigSchema_json);

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

#if WITH_IHV_ORT_GENAI
  ADD_ASSET_MAP_ENTRY(kOrtGenAI, IHV_OrtGenAI_dll);
#endif

#else


ADD_ASSET_MAP_ENTRY(kEPDependenciesConfig, ep_dependencies_config_macOS_json);



#endif

  return memory_map;
}

Unpacker::Unpacker() { deps_dir_ = CreateDepsDirectory(); }

Unpacker::~Unpacker() = default;

void Unpacker::SetDepsDir(const std::string& deps_dir) {
  auto old_dir = deps_dir_;
  if (!deps_dir.empty()) deps_dir_ = CreateDepsDirectory(deps_dir);
  // move unpacked files from the old dir to the new dir
  if (deps_dir_ != old_dir) {
    for (const auto& path : unpacked_files_) {
      fs::path new_path = deps_dir_ / path.filename();
      fs::rename(path, new_path);
    }
    if (fs::is_empty(old_dir)) fs::remove(old_dir);
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
    case kOrtGenAI:
      adjust_for_ep(EP::kIHVOrtGenAI);
      break;
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
    ;
    return ExtractFileFromMemory(memory_ptr, file_size, file_parts,
                                 file_parts_size, dest_file.string());
  };

  switch (asset) {
    case kLog4cxxConfig:
    case kConfigSchema:
    case kLlama2InputFileSchema:
    case kOutputResultsSchema:
    case kDataVerificationFile:
    case kDataVerificationFileSchema:
    case kConfigVerificationFile:
    case kEPDependenciesConfig:
    case kEPDependenciesConfigSchema:
#if defined(_WIN32) && WITH_IHV_NATIVE_OPENVINO
    case kNativeOpenVINO:
#endif  // WITH_IHV_NATIVE_OPENVINO
#if defined(_WIN32) && WITH_IHV_ORT_GENAI
    case kOrtGenAI:
#endif
      return unpack_regular_file();
    default:
      LOG4CXX_ERROR(loggerUnpacker, "Unknown packed file type: " << (int)asset);
      return false;
  }
}

size_t Unpacker::GetAllDataSize() const {
  size_t data_size = 0;

  data_size += assets::Log4CxxConfig_xmlSize;
  data_size += assets::ConfigSchema_jsonSize;
  data_size += assets::OutputResultsSchema_jsonSize;
  data_size += assets::data_verification_jsonSize;
  data_size += assets::DataVerificationSchema_jsonSize;

#ifdef _WIN32
#if !defined(_M_ARM64) && !defined(_M_ARM)
  data_size += assets::ep_dependencies_config_windows_x64_jsonSize;

#else
  data_size += assets::ep_dependencies_config_windows_ARM_jsonSize;
#endif

#else
  data_size += assets::ep_dependencies_config_macOS_jsonSize;
#endif

  data_size += assets::EPDependenciesConfigSchema_jsonSize;

  return data_size;
}

std::vector<std::string> Unpacker::UnpackFilesFromZIP(
    const std::string& zip_file, const std::string& dest_dir,
    int64_t timeout_ms) {
  if (!fs::exists(zip_file)) {
    LOG4CXX_ERROR(loggerUnpacker, "Zip file does not exist: " << zip_file);
    return {};
  }

  auto free_space = utils::GetAvailableDiskSpace(dest_dir);

  auto check_size = [&free_space,
                     &zip_file](std::uintmax_t total_uncompressed_size) {
    if (free_space >= total_uncompressed_size) return;
    std::stringstream error_ss{"Don't have enough space to unpack "};
    error_ss << zip_file << ". Free space " << std::to_string(free_space);
    error_ss << ", uncompressed files size "
             << std::to_string(total_uncompressed_size);
    auto error_string = error_ss.str();
    LOG4CXX_DEBUG(loggerUnpacker, error_string.c_str());
    throw std::runtime_error("Don't have enough space to unpack");
  };

  auto calculate_size = [zip_file]() {
    LOG4CXX_DEBUG(
        loggerUnpacker,
        "Calculating total uncompressed size of zip file: " << zip_file);
    std::uint64_t total_uncompressed_size = 0;
    total_uncompressed_size = MinizipCalculateTotalUncompressedSize(zip_file);
    if (total_uncompressed_size == 0) {
      LOG4CXX_DEBUG(loggerUnpacker,
                    "Failed to calculate total uncompressed "
                    "size using minizip");
#ifdef _WIN32
      total_uncompressed_size = WinApiCalculateTotalUncompressedSize(zip_file);
      if (total_uncompressed_size == 0) {
        LOG4CXX_DEBUG(loggerUnpacker,
                      "Failed to calculate total uncompressed "
                      "size using WinAPI");
      }
#endif
    }
    return total_uncompressed_size;
  };

  auto unzip = [zip_file, dest_dir]() {
    LOG4CXX_DEBUG(loggerUnpacker,
                  "Unpacking files from zip file: " << zip_file);
    std::vector<std::string> unzipped_files =
        UnZipUsingMinizip(zip_file, dest_dir);

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

    } catch (const std::exception& e) {
      throw std::runtime_error("Failed to get file size (" +
                               std::string(e.what()) + ")");
    }
    if (zip_size == 0) {
      throw std::runtime_error("Zip file is empty");
    }
    check_size(zip_size);

    {
      std::promise<std::uint64_t> promise;
      auto future = promise.get_future();
      std::thread worker([&promise, calculate_size]() {
        try {
          auto result = calculate_size();
          promise.set_value(result);
        } catch (...) {
          promise.set_exception(std::current_exception());
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

      zip_size = future.get();
    }

    check_size(zip_size);

    if (timeout_ms == 0) return unzip();

    std::chrono::milliseconds timeout_duration;
    if (timeout_ms < 0) {
      try {
        const double file_size_mb =
            static_cast<double>(zip_size) / (1024.0 * 1024.0);
        timeout_duration = std::chrono::milliseconds(
            static_cast<int64_t>(file_size_mb * kTimeoutPerMegabyte));
        if (timeout_duration < kMinTimeoutDuration) {
          timeout_duration = kMinTimeoutDuration;
        } else if (timeout_duration > kMaxTimeoutDuration) {
          timeout_duration = kMaxTimeoutDuration;
        }
      } catch (const std::exception& e) {
        std::string error_msg = "Failed to get file size (";
        error_msg += e.what();
        error_msg += ")";
        throw std::runtime_error(error_msg);
      }
    } else if (timeout_ms > 0) {
      timeout_duration = std::chrono::milliseconds(timeout_ms);
    }

    {
      std::promise<std::vector<std::string>> promise;
      auto future = promise.get_future();
      std::thread worker([&promise, &unzip]() {
        try {
          auto result = unzip();
          promise.set_value(result);
        } catch (...) {
          promise.set_exception(std::current_exception());
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
  if (success) unpacked_files_.push_back(file_path);
  return success;
}

}  // namespace cil
