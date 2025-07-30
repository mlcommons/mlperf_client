#include "utils.h"

#include <log4cxx/logger.h>
#include <openssl/evp.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>

using namespace log4cxx;

LoggerPtr loggerUtils(Logger::getLogger("Utils"));

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

#pragma comment(lib, "setupapi.lib")

#include <regstr.h>
#include <setupapi.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

#undef GetCurrentDirectory
#undef SetCurrentDirectory
#undef CreateDirectory
#undef GetTempPath

namespace cil {
namespace utils {

#ifdef _WIN32
LibraryPathHandle AddLibraryPath(const fs::path& path) {
  auto cookie = AddDllDirectory(path.c_str());
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  return {cookie};
}
bool RemoveLibraryPath(const LibraryPathHandle& handle) {
  return !!RemoveDllDirectory(handle.cookie_);
}
#else
LibraryPathHandle AddLibraryPath(const fs::path& path) {
  // On Unix-like systems, modifying LD_LIBRARY_PATH at runtime affects only
  // subprocesses. It's generally better to ensure the dynamic linker knows
  // where to find your libraries ahead of time.
  std::string newPath = path.string();
  const char* existingPath = std::getenv("LD_LIBRARY_PATH");
  if (existingPath != nullptr) {
    newPath += ":" + std::string(existingPath);
  }
  setenv("LD_LIBRARY_PATH", newPath.c_str(), 1);
  return {path};
}
bool RemoveLibraryPath(const LibraryPathHandle& handle) {
  const char* existingPath = std::getenv("LD_LIBRARY_PATH");
  if (existingPath != nullptr) {
    std::string newPath(existingPath);
    newPath.erase(newPath.find(handle.path_.string()),
                  handle.path_.string().length());
    setenv("LD_LIBRARY_PATH", newPath.c_str(), 1);
    return true;
  }

  return false;
}
#endif

std::string GetCurrentDirectory() {
  char buffer[1024];
  std::string path = "";

#if defined(_WIN32) || defined(_WIN64)
  GetModuleFileName(NULL, buffer, sizeof(buffer));
  path = buffer;
#elif defined(__APPLE__)
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) path = buffer;
#endif

  // Remove the executable name from the path
  size_t last_slash_idx = path.find_last_of("\\/");
  if (std::string::npos != last_slash_idx) {
    path = path.substr(0, last_slash_idx);
  }

  return path;
}

void SetCurrentDirectory(const std::string& path) {
  std::string targetPath = path.empty() ? utils::GetCurrentDirectory() : path;
#if defined(_WIN32) || defined(_WIN64)
  SetCurrentDirectoryA(targetPath.c_str());
#else
  chdir(targetPath.c_str());
#endif
}

bool CreateDirectory(const std::string& directoryPath) {
  try {
    std::filesystem::create_directories(directoryPath);
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerUtils, "Failed to create directory. Path: "
                                   << directoryPath
                                   << ", Exception: " << e.what());
    return false;
  }

  return true;
}

bool CreateUniqueDirectory(const fs::path& base_dir, fs::path& out_path,
                           int max_attempts) {
  std::string time_str = GetCurrentDateTimeString("%m%d%Y%H%M%S");

#if defined(_WIN32) || defined(_WIN64)
  auto pid = _getpid();
#else
  auto pid = getpid();
#endif

  for (int attempts = 0; attempts < max_attempts; ++attempts) {
    std::string folder_name =
        time_str + "_" + std::to_string(pid) + "_" + std::to_string(attempts);
    fs::path unique_path = base_dir / folder_name;

    if (fs::create_directories(unique_path)) {
      out_path = unique_path;
      return true;
    }
  }

  return false;
}

bool IsDirectoryWritable(const std::string& path) {
  if (path.empty()) return false;

  std::filesystem::path dir_path(path);
  if (!std::filesystem::exists(dir_path) ||
      !std::filesystem::is_directory(dir_path))
    return false;

  std::filesystem::path test_file = dir_path / "mlperf_tmp_writable_test.txt";
  std::ofstream ofs(test_file.string(), std::ios::out | std::ios::trunc);
  if (!ofs.is_open()) return false;

  ofs.close();
  std::error_code ec;
  std::filesystem::remove(test_file, ec);
  return true;
}

void SaveFlagToSettings(const std::string& settings_path,
                        const std::string& value_name, bool flag) {
#if defined(_WIN32) || defined(_WIN64)
  HKEY hKey = NULL;
  LONG result = 0;

  result = RegCreateKeyExA(HKEY_CURRENT_USER, settings_path.c_str(), 0, NULL, 0,
                           KEY_SET_VALUE, NULL, &hKey, NULL);
  if (ERROR_SUCCESS != result) {
    LOG4CXX_ERROR(loggerUtils,
                  "Failed to create or open the registry key. Key: "
                      << settings_path << ", Result: " << result);
    return;
  }

  DWORD flagValue = flag ? 1 : 0;
  result = RegSetValueExA(hKey, value_name.c_str(), 0, REG_DWORD,
                          reinterpret_cast<BYTE*>(&flagValue), sizeof(DWORD));
  if (ERROR_SUCCESS != result) {
    LOG4CXX_ERROR(loggerUtils, "Failed to set the registry value. Key: "
                                   << settings_path << ", Value Name: "
                                   << value_name << ", Result: " << result);
  }

  RegCloseKey(hKey);
#else
  CFStringRef app_id = CFStringCreateWithCString(nullptr, settings_path.c_str(),
                                                 kCFStringEncodingUTF8);
  CFStringRef key = CFStringCreateWithCString(nullptr, value_name.c_str(),
                                              kCFStringEncodingUTF8);

  CFPreferencesSetAppValue(key, flag ? kCFBooleanTrue : kCFBooleanFalse,
                           app_id);
  Boolean success = CFPreferencesSynchronize(app_id, kCFPreferencesCurrentUser,
                                             kCFPreferencesAnyHost);

  if (!success)
    LOG4CXX_ERROR(loggerUtils, "Failed to save preference value. Key: "
                                   << settings_path
                                   << ", Value Name: " << value_name);

  CFRelease(app_id);
  CFRelease(key);
#endif
}

bool ReadFlagFromSettings(const std::string& settings_path,
                          const std::string& value_name) {
  bool flagValue = false;

#if defined(_WIN32) || defined(_WIN64)
  HKEY hKey;
  LONG result;

  result = RegOpenKeyExA(HKEY_CURRENT_USER, settings_path.c_str(), 0,
                         KEY_QUERY_VALUE, &hKey);
  if (ERROR_SUCCESS == result) {
    DWORD dataSize = sizeof(DWORD);
    DWORD dwValue;
    result = RegQueryValueExA(hKey, value_name.c_str(), NULL, NULL,
                              reinterpret_cast<BYTE*>(&dwValue), &dataSize);
    if (ERROR_SUCCESS == result) {
      flagValue = (dwValue == 1);
    } else {
      LOG4CXX_ERROR(
          loggerUtils,
          "Failed to read the registry value or the key does not exist. Key: "
              << settings_path << ", Value Name: " << value_name
              << ", Result: " << result);
    }

    RegCloseKey(hKey);
  } else {
    LOG4CXX_ERROR(loggerUtils, "Failed to open the registry key. Key: "
                                   << settings_path << ", Result: " << result);
  }
  return flagValue;
#else
  CFStringRef app_id = CFStringCreateWithCString(nullptr, settings_path.c_str(),
                                                 kCFStringEncodingUTF8);
  CFStringRef key = CFStringCreateWithCString(nullptr, value_name.c_str(),
                                              kCFStringEncodingUTF8);

  Boolean key_is_valid;
  Boolean read_value =
      CFPreferencesGetAppBooleanValue(key, app_id, &key_is_valid);

  CFRelease(app_id);
  CFRelease(key);

  if (!key_is_valid) {
    LOG4CXX_ERROR(
        loggerUtils,
        "Failed to read the preference value or the key does not exist. Key: "
            << settings_path << ", Value Name: " << value_name);
    return false;
  }

  return static_cast<bool>(read_value);
#endif
}

std::string GetCurrentDateTimeString(const std::string& format) {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << std::put_time(std::localtime(&in_time_t), format.c_str());
  return ss.str();
}

std::string GetDateTimeString(
    std::chrono::high_resolution_clock::time_point time_point) {
  auto now = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      time_point - std::chrono::high_resolution_clock::now() +
      std::chrono::system_clock::now());

  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  std::tm* local_tm = std::localtime(&in_time_t);

  std::stringstream ss;
  // Print with milliseconds "02-25-2025 17:38:15.269"
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) %
                1000;
  ss << std::put_time(local_tm, "%m-%d-%Y %H:%M:%S.") << '.'
     << std::setfill('0') << std::setw(3) << millis.count();

  char timezone[6];  // Buffer for timezone, e.g, +0400
  std::strftime(timezone, sizeof(timezone), "%z", local_tm);
  std::string temezone_formatted = std::string(timezone).insert(3, ":");

  ss << " " << temezone_formatted;

  return ss.str();
}

#if defined(__APPLE__)
bool IsDebuggerPresent() {
  int mib[4];
  struct kinfo_proc info;
  size_t size;

  info.kp_proc.p_flag = 0;

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;
  mib[3] = getpid();

  size = sizeof(info);
  if (sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0) != 0)
    return false;

  // THe app is being debugged if the P_TRACED flag is set.
  return (info.kp_proc.p_flag & P_TRACED) != 0;
}
#endif

int GetConsoleWidth() {
#if defined(_WIN32) || defined(_WIN64)
  CONSOLE_SCREEN_BUFFER_INFO bufferInfo;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),
                                 &bufferInfo)) {
    return bufferInfo.dwSize.X;
  }
  // Unable to get width
  return 0;
#else
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1) {
    return w.ws_col;
  }
  return 0;
#endif
}

size_t GetAvailableDiskSpace(const std::string& path) {
  try {
    auto space_info = std::filesystem::space(path);
    return space_info.available;
  } catch (...) {
    return -1;
  }
}

std::string GetFileNameFromPath(const std::string& path) {
  return std::filesystem::path(path).filename().string();
}

bool FileExists(const std::string& path) {
  try {
    return std::filesystem::exists(path);
  } catch (...) {
    return false;
  }
}

std::string ComputeFileSHA256(const std::string& file_path,
                              log4cxx::LoggerPtr logger) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    if (logger) {
      LOG4CXX_ERROR(logger, "Failed to open file: " << file_path);
    }
    return "";
  }
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) {
    if (logger) {
      LOG4CXX_ERROR(logger, "Failed to create digest context");
    }
    return "";
  }
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    if (logger) {
      LOG4CXX_ERROR(logger, "Failed to initialize digest context");
    }
    EVP_MD_CTX_free(ctx);
    return "";
  }
  char buffer[BUFSIZ];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
      if (logger) {
        LOG4CXX_ERROR(logger, "Failed to update digest");
      }
      EVP_MD_CTX_free(ctx);
      return "";
    }
  }
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    if (logger) {
      LOG4CXX_ERROR(logger, "Failed to finalize digest");
    }
    EVP_MD_CTX_free(ctx);
    return "";
  }
  EVP_MD_CTX_free(ctx);
  // Convert hash to hexadecimal string.
  std::stringstream hash_string;
  hash_string << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < hash_len; ++i) {
    hash_string << std::setw(2) << static_cast<int>(hash[i]);
  }
  return hash_string.str();
}

std::string ExtractFilenameFromPath(const std::string& path) {
  return std::filesystem::absolute(path).filename().string();
}
std::string CleanErrorMessageFromStaticPaths(const std::string& message) {
  // This regex looks for file paths in the message
  std::regex path_regex(
      R"((?:[a-zA-Z]:)?(?:\\|\/)(?:[^\\\/:*?"<>|]+(?:\\|\/))+[^\\\/:*?"<>|]+)");

  std::string result;

  std::sregex_iterator begin(message.begin(), message.end(), path_regex);
  std::sregex_iterator end;
  size_t last_pos = 0;
  for (std::sregex_iterator i = begin; i != end; ++i) {
    std::smatch match = *i;
    // Append the text before the match
    result += message.substr(last_pos, match.position() - last_pos);
    // Extract the file name from the path
    result += ExtractFilenameFromPath(match.str());
    last_pos = match.position() + match.length();
  }
  result += message.substr(last_pos);

  return result;
}
bool IsValidPath(const std::string path) {
  std::string path_to_validate = path;
#ifdef _WIN32
  std::regex valid_chars_pattern(
      R"(^([a-zA-Z]:\\|[a-zA-Z]:\/)?([^<>:"|?*]*)$)");
#else
  std::regex valid_chars_pattern(R"([^:]*$)");
#endif
  return std::regex_search(path_to_validate, valid_chars_pattern);
}
std::string NormalizePath(const std::string& path) {
  std::string normalized_path;
  // We should not expect escape characters in the path
  for (const char c : path) {
    switch (c) {
      case '\a':
        normalized_path += "\\a";
        break;
      case '\b':
        normalized_path += "\\b";
        break;
      case '\f':
        normalized_path += "\\f";
        break;
      case '\n':
        normalized_path += "\\n";
        break;
      case '\r':
        normalized_path += "\\r";
        break;
      case '\t':
        normalized_path += "\\t";
        break;
      case '\v':
        normalized_path += "\\v";
        break;
      default:
        normalized_path += c;
        break;
    }
  }
  std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
  return normalized_path;
}

std::string StringToLowerCase(const std::string& input_string) {
  std::string lower_case_str;
  std::transform(input_string.begin(), input_string.end(),
                 std::back_inserter(lower_case_str),
                 [](unsigned char c) { return std::tolower(c); });
  return lower_case_str;
}

std::string StringReplaceChar(const std::string& input_string,
                              unsigned char find, unsigned char replace) {
  std::string lower_case_str;
  std::transform(input_string.begin(), input_string.end(),
                 std::back_inserter(lower_case_str),
                 [&](unsigned char c) { return c == find ? replace : c; });
  return lower_case_str;
}

bool IsUtf8(const std::string& str) {
  size_t i = 0;
  while (i < str.size()) {
    uint8_t c = static_cast<uint8_t>(str[i]);
    // get the number of bytes in the UTF-8 character
    int bytes = 0;
    if (c < 0x80) {
      bytes = 1;
    } else if ((c & 0xE0) == 0xC0) {
      bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
      bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
      bytes = 4;
    } else {
      return false;
    }
    // check if the next bytes are valid
    for (int j = 1; j < bytes; j++) {
      if (i + j >= str.size()) {
        return false;
      }
      c = static_cast<uint8_t>(str[i + j]);
      if ((c & 0xC0) != 0x80) {
        return false;
      }
    }
    // The character is valid
    i += bytes;
  }
  return true;
}

std::string SanitizeToUtf8(const std::string& str) {
  std::string sanitized_str;
  sanitized_str.reserve(str.size());

  for (size_t i = 0; i < str.size(); i++) {
    uint8_t c = static_cast<uint8_t>(str[i]);
    // get the number of bytes in the UTF-8 character
    int bytes = 0;
    if (c < 0x80) {
      bytes = 1;
    } else if ((c & 0xE0) == 0xC0) {
      bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
      bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
      bytes = 4;
    } else {
      // Invalid UTF-8 character
      continue;
    }
    // check if the next bytes are valid
    for (int j = 1; j < bytes; j++) {
      if (i + j >= str.size()) {
        // Invalid UTF-8 character
        continue;
      }
      c = static_cast<uint8_t>(str[i + j]);
      if ((c & 0xC0) != 0x80) {
        // Invalid UTF-8 character
        continue;
      }
    }
    // The character is valid
    sanitized_str += str.substr(i, bytes);
    i += bytes - 1;
  }
  return sanitized_str;
}

#ifdef _WIN32

static std::string GetVendorString(VendorID vendor) {
  switch (vendor) {
    case VendorID::kNVIDIA:
      return "VEN_10DE";
    case VendorID::kIntel:
      return "VEN_8086";
    case VendorID::kAMD:
      return "VEN_1002";
    case VendorID::kQualcomm:
      return "VEN_17CB";
    default:
      return "UNKNOWN";
  }
}

static VendorID GetVendorIDFromString(const std::string& vendorString) {
  if (vendorString.find("VEN_10DE") != std::string::npos) {
    return VendorID::kNVIDIA;
  } else if (vendorString.find("VEN_8086") != std::string::npos) {
    return VendorID::kIntel;
  } else if (vendorString.find("VEN_1002") != std::string::npos ||
             vendorString.find("VEN_1022") != std::string::npos) {
    return VendorID::kAMD;
  } else if (vendorString.find("VEN_17CB") != std::string::npos) {
    return VendorID::kQualcomm;
  } else if (vendorString.find("VEN_15B3") != std::string::npos) {
    return VendorID::kMellanox;
  } else {
    return VendorID::kUnknown;
  }
}

std::vector<VendorID> GetAvailableVendorIDs() {
  std::vector<VendorID> vendors;
  HDEVINFO deviceInfoSet;
  SP_DEVINFO_DATA deviceInfoData;
  DWORD i;

  // Create a HDEVINFO with all present devices.
  deviceInfoSet = SetupDiGetClassDevs(NULL, REGSTR_KEY_PCIENUM, NULL,
                                      DIGCF_PRESENT | DIGCF_ALLCLASSES);

  if (deviceInfoSet == INVALID_HANDLE_VALUE) return vendors;

  // Enumerate through all devices in Set.
  deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  for (i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
    DWORD DataT;
    char* buffer = NULL;
    DWORD buffersize = 0;

    // Get VendorID and DeviceID
    while (!SetupDiGetDeviceRegistryProperty(
        deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, &DataT, (PBYTE)buffer,
        buffersize, &buffersize)) {
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        if (buffer) LocalFree(buffer);
        buffer = (char*)LocalAlloc(LPTR, buffersize);
      } else {
        break;
      }
    }

    if (buffer) {
      std::string hwid(buffer);
      VendorID vendor = GetVendorIDFromString(hwid);
      if (vendor != VendorID::kUnknown) {
        vendors.push_back(vendor);
      }
      LocalFree(buffer);
    }
  }

  SetupDiDestroyDeviceInfoList(deviceInfoSet);
  return vendors;
}

bool SupportsVendorID(VendorID vendor_id) {
  auto vendors = GetAvailableVendorIDs();
  return std::find(vendors.begin(), vendors.end(), vendor_id) != vendors.end();
}

#else

std::vector<VendorID> GetAvailableVendorIDs() { return {VendorID::kApple}; }

bool SupportsVendorID(VendorID vendor_id) {
  return vendor_id == VendorID::kApple;
}

#endif

std::string PrependBaseDirIfNeeded(const std::string& path,
                                   const std::string& base_dir) {
  if (base_dir.empty() || path.empty() || path.find("http://") == 0 ||
      path.find("https://") == 0) {
    return path;
  }

  std::filesystem::path file_path =
      path.find("file://") == 0 ? path.substr(7) : path;
  if (!file_path.is_relative()) return path;

  file_path = std::filesystem::absolute(base_dir) / file_path;

  // Convert the path to string and make it start with file://
  return "file://" + file_path.string();
}

std::string FormatDuration(const std::chrono::steady_clock::duration& duration,
                           bool with_millisec) {
  auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
  auto minutes =
      std::chrono::duration_cast<std::chrono::minutes>(duration - hours);
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
      duration - hours - minutes);
  auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      duration - hours - minutes - seconds);

  std::ostringstream oss;
  oss << std::setw(2) << std::setfill('0') << hours.count() << ":"
      << std::setw(2) << std::setfill('0') << minutes.count() << ":"
      << std::setw(2) << std::setfill('0') << seconds.count();
  if (with_millisec) {
    oss << "." << std::setw(3) << std::setfill('0') << milliseconds.count();
  }
  return oss.str();
}

std::string CleanAndTrimString(std::string str) {
  std::replace(str.begin(), str.end(), '\0', ' ');
  static auto is_space = [](auto ch) { return std::isspace(ch); };
  str.erase(str.begin(), std::find_if_not(str.begin(), str.end(), is_space));
  str.erase(std::find_if_not(str.rbegin(), str.rend(), is_space).base(),
            str.end());
  return str;
}

bool IsEpSupportedOnThisPlatform(const std::string_view& model_name,
                                 const std::string_view& ep_name) {
  if (!model_name.empty() && model_name != "llama2" && model_name != "llama3" &&
      model_name != "phi3.5" && model_name != "phi4")
    return false;

  auto is_supported_ihv = [&](const std::string& name) {
    return ep_name == name || ep_name == "IHV " + name;
  };

#if WITH_IHV_NATIVE_OPENVINO
  if (is_supported_ihv("NativeOpenVINO")) return true;
#endif

#if WITH_IHV_NATIVE_QNN
  if (is_supported_ihv("NativeQNN")) return true;
#endif
#if WITH_IHV_ORT_GENAI
  if (is_supported_ihv("OrtGenAI")) return true;

#endif
#if WITH_IHV_ORT_GENAI_RYZENAI
  if (is_supported_ihv("OrtGenAI-RyzenAI")) return true;
#endif
#if WITH_IHV_WIN_ML
  if (is_supported_ihv("WindowsML")) return true;

#endif
#if WITH_IHV_GGML_METAL
  if (is_supported_ihv("Metal")) return true;
#endif
#if WITH_IHV_GGML_VULKAN
  if (is_supported_ihv("Vulkan")) return true;
#endif
#if WITH_IHV_GGML_CUDA
  if (is_supported_ihv("CUDA")) return SupportsVendorID(VendorID::kNVIDIA);
#endif
#if WITH_IHV_MLX
  if (is_supported_ihv("MLX")) return true;
#endif

  return false;
}

bool IsEpConfigSupportedOnThisPlatform(
    const std::string_view& config_file_name) {
#if defined(__APPLE__)
  const std::string config = StringToLowerCase(std::string(config_file_name));
#if TARGET_OS_IOS
  return config.starts_with("ios");
#else
  return config.starts_with("macos");
#endif
#else
  return !config_file_name.empty();
#endif
}

}  // namespace utils
}  // namespace cil
