#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "IHV/IHV.h"
#include "utils.h"

#ifndef IHV_SUBPROCESS
#define IHV_SUBPROCESS 0
#endif

class dylib;

namespace cil {

class API_Handler {
 public:
  enum class LogLevel {
    kInfo,
    kWarning,
    kError,
    kFatal,
  };

  using Logger = std::function<void(LogLevel, std::string)>;
  using DeviceListPtr = const API_IHV_DeviceList_t*;

#if IHV_SUBPROCESS
  static void SetDefaultSubprocessMode(bool enabled);
  static bool GetDefaultSubprocessMode();

  static int RunSubprocessClient(const std::string& token);
#endif

  API_Handler(const std::string& library_path, Logger logger,
              bool force_unload_ep_deps = false
#if IHV_SUBPROCESS
              ,
              std::optional<bool> use_subprocess = std::nullopt
#endif
  );
  virtual ~API_Handler();

  API_Handler(const API_Handler&) = delete;
  API_Handler& operator=(const API_Handler&) = delete;

  bool IsLoaded() const;

  bool Setup(const std::string& ep_name, const std::string& model_name,
             const std::string& model_path, const std::string& deps_dir,
             const nlohmann::json& ep_settings, std::string& device_type_out);
  // Enumerate available devices
  bool EnumerateDevices(DeviceListPtr& device_list);
  // Load model on specified device (or default if not provided)
  bool Init(const std::string& model_config = std::string(),
            std::optional<API_IHV_DeviceID_t> device_id = std::nullopt);
  bool Prepare();                          // Setup inference
  bool Infer(API_IHV_IO_Data_t& io_data);  // Run inference
  bool Reset();                            // Reset inference
  bool Deinit();                           // Clear loaded model
  bool Release();                          // Release the library

  static std::string CanBeLoaded(
      const std::string& library_path
#if IHV_SUBPROCESS
      ,
      std::optional<bool> use_subprocess = std::nullopt
#endif
  );

  struct DeviceInfo {
    API_IHV_DeviceID_t device_id;
    std::string device_name;
  };

  typedef std::vector<DeviceInfo> DeviceList;

  static DeviceList EnumerateDevices(
      const std::string& library_path, const std::string& ep_name,
      const std::string& model_name, const nlohmann::json& ep_settings,
      std::string& error, std::string& log
#if IHV_SUBPROCESS
      ,
      std::optional<bool> use_subprocess = std::nullopt
#endif
  );

  std::string GetIHVErrors() const { return ihv_errors_; }
  void ClearErrorMessage() { ihv_errors_.clear(); }

 private:
  static void Log(void* context, API_IHV_LogLevel level, const char* message);

  std::string library_path_;

  const struct API_IHV_Struct_t* ihv_struct_ = nullptr;

  Logger logger_;

  API_IHV_Setup_func setup_ = nullptr;
  API_IHV_EnumerateDevices_func enumerate_devices_ = nullptr;
  API_IHV_Init_func init_ = nullptr;
  API_IHV_Prepare_func prepare_ = nullptr;
  API_IHV_Infer_func infer_ = nullptr;
  API_IHV_Reset_func reset_ = nullptr;
  API_IHV_Deinit_func deinit_ = nullptr;
  API_IHV_Release_func release_ = nullptr;

  std::unique_ptr<dylib> library_;
  utils::LibraryPathHandle library_path_handle_;
  const bool force_unload_ep_;
  std::string library_path_directory_;

  std::string ihv_errors_;

#if IHV_SUBPROCESS
  static bool s_default_subprocess_mode_;

  class IPC;
  class Server;
  class Client;

  const bool use_subprocess_;
  std::unique_ptr<Server> subprocess_server_;
#endif
};

}  // namespace cil
