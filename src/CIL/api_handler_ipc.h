#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "IHV/IHV.h"
#include "api_handler.h"

#if IHV_SUBPROCESS

namespace cil {

// Shared IPC timeouts (milliseconds).
constexpr int kIPCTimeout = 60 * 1000;           // connection, shutdown, probe
constexpr int kIPCTimeoutLong = 45 * 60 * 1000;  // init, setup, prepare, infer

// Per-channel kernel buffer for the IPC sockets/pipes.
constexpr int kPipeBufferSize = 1024 * 1024;  // 1 MiB

// Base of the IHV subprocess IPC. Holds the message protocol; the platform
// transport (named pipes on Windows, Unix-domain sockets on POSIX) lives
// behind the Impl pimpl, so this header pulls in no platform headers.
class API_Handler::IPC {
 public:
  enum class MessageType : uint32_t {
    kInvalid = 0,
    kPing = 1,
    kShutdown = 2,

    kResult = 100,
    kLog = 101,
    kToken = 102,
    kImageStep = 103,

    kCanBeLoaded = 200,
    kSetup = 201,
    kEnumerateDevices = 202,
    kInit = 203,
    kPrepare = 204,
    kInfer = 205,
    kReset = 206,
    kDeinit = 207,
    kRelease = 208,
  };

  struct MessageHeader {
    uint32_t length;
    MessageType message_type;
    uint64_t token;
    uint32_t payload_size;
  };

  static uint64_t GenerateToken();
  static std::string TokenToString(uint64_t token);
  static uint64_t StringToToken(const std::string& str);

 protected:
  // Identifies one of the three IPC channels; the platform layer maps it to
  // the underlying handle/descriptor.
  enum class Channel { kCommand, kResponse, kLog };

  // Platform-specific transport state, defined in the per-platform .cpp.
  struct Impl;

  explicit IPC(uint64_t token);
  virtual ~IPC();

  // Transport primitives - defined per platform.
  bool WriteMessage(Channel channel, MessageType message_type,
                    std::span<const std::byte> payload = {}) const;
  std::optional<std::pair<MessageType, nlohmann::json>> ReadMessage(
      Channel channel, int timeout_ms);

  // Decodes a fully-received framed message (header + payload). Shared by both
  // platform transports.
  static std::optional<std::pair<MessageType, nlohmann::json>> ParseMessage(
      const std::byte* data, size_t size, uint64_t token);

  uint64_t token_ = 0;
  std::unique_ptr<Impl> impl_;
};

// Runs in the parent process: spawns the IHV subprocess and drives it.
class API_Handler::Server : public API_Handler::IPC {
 public:
  Server(API_Handler::Logger& logger, std::string& ihv_errors);
  ~Server() final;

  bool Start();
  void Stop();
  bool IsRunning() const;

  nlohmann::json SendMessage(MessageType message, const nlohmann::json& payload,
                             int timeout_ms = kIPCTimeoutLong);

  using TokenCallback = std::function<void(uint32_t)>;
  using ImageStepCallback = std::function<void(uint32_t, uint32_t, uint32_t)>;
  nlohmann::json SendInferCommand(const nlohmann::json& payload,
                                  const TokenCallback& token_callback,
                                  const ImageStepCallback& image_step_callback,
                                  int timeout_ms = kIPCTimeoutLong);

  bool HandleResponse(
      const nlohmann::json& response, const std::string& operation,
      API_Handler::LogLevel level = API_Handler::LogLevel::kError);

  uint64_t GetToken() const { return token_; }

 private:
  // API_Handler drives this Server directly and fills/consumes the scratch
  // buffers below, so it is granted access to the private members.
  friend class API_Handler;

  // Platform transport - defined per platform.
  bool CreatePipes();
  void ClosePipes();
  bool SpawnProcess(const std::string& executable_path);
  bool WaitForClientConnection(int timeout_ms);
  void StopProcess();
  void LogReaderThread();
  void StdoutReaderThread();

  // Parses one framed kLog message and forwards it to the logger.
  void HandleLogBytes(const std::byte* data, size_t size) const;

  API_Handler::Logger& logger_;
  std::string& ihv_errors_;
  std::atomic<bool> running_{false};
  std::thread log_reader_thread_;
  std::thread stdout_reader_thread_;
  std::atomic<bool> stop_log_reader_{false};

  // Scratch buffers owned by the Server but filled/consumed by API_Handler
  // during device enumeration and inference; kept alive across IHV calls.
  std::vector<uint8_t> device_list_storage_;
  std::vector<uint32_t> infer_output_buffer_;
  std::vector<uint8_t> infer_output_bytes_;
};

// Runs in the spawned subprocess: connects back to the parent and executes
// IHV commands against a local (non-subprocess) API_Handler.
class API_Handler::Client : public API_Handler::IPC {
 public:
  static int Run(const std::string& token_str);

 private:
  explicit Client(uint64_t token);
  ~Client() final;

  // Platform transport - defined per platform.
  bool Connect();

  void ProcessCommands();
  bool HandleMessage(MessageType message, const nlohmann::json& payload);
  void SendResponse(bool success, const nlohmann::json& data = {}) const;
  void SendLog(API_IHV_LogLevel level, const std::string& message) const;
  void SendToken(uint32_t token) const;
  void SendImageStep(uint32_t phase, uint32_t step,
                     uint32_t total_steps) const;

  static void TokenCallback(void* object, uint32_t token) {
    assert(object != nullptr);
    static_cast<Client*>(object)->SendToken(token);
  }

  static void ImageStepCallbackStatic(void* object,
                                      const API_IHV_ImageStep_Event_t* event) {
    assert(object != nullptr);
    assert(event != nullptr);
    // Submodule events are not forwarded across the IPC subprocess boundary
    // for now; only lifecycle/step events round-trip.
    if (event->phase == API_IHV_ImageStep_Submodule) return;
    static_cast<Client*>(object)->SendImageStep(
        static_cast<uint32_t>(event->phase), event->step, event->total_steps);
  }

  void SendResult(bool result, const std::string& operation = "") const {
    if (result) {
      SendResponse(true);
    } else {
      nlohmann::json data = {{"ihv_errors", local_handler_->GetIHVErrors()}};
      if (!operation.empty()) {
        data["error"] = operation + " failed";
      }
      SendResponse(false, data);
    }
  }

  std::unique_ptr<API_Handler> local_handler_;

  std::string library_path_;
  std::string ep_name_;
  std::string scenario_name_;
  std::string model_base_name_;
  std::string model_path_;
  std::string deps_dir_;
  nlohmann::json ep_settings_;
  std::string device_type_;
};

}  // namespace cil

#endif  // IHV_SUBPROCESS
