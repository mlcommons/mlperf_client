#include "api_handler.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <span>
#include <sstream>
#include <thread>

#include "dylib.hpp"
#include "version.h"

#ifdef _WIN32
#include <psapi.h>
#include <windows.h>
#undef SendMessage
#endif

#define MAX_DEVICE_TYPE_LENGTH 16

namespace cil {

#if IHV_SUBPROCESS

constexpr int kIPCTimeout = 5000;  // 5s - connection, shutdown
constexpr int kIPCTimeoutLong =
    30 * 60 * 1000;  // 30min - init, setup, prepare, etc.
constexpr DWORD kPipeBufferSize = 65536;

bool API_Handler::s_default_subprocess_mode_ = false;

void API_Handler::SetDefaultSubprocessMode(bool enabled) {
  s_default_subprocess_mode_ = enabled;
}

bool API_Handler::GetDefaultSubprocessMode() {
  return s_default_subprocess_mode_;
}

class API_Handler::IPC {
 public:
  enum class MessageType : uint32_t {
    kInvalid = 0,
    kPing = 1,
    kShutdown = 2,

    kResult = 100,
    kLog = 101,
    kToken = 102,

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

  static uint64_t GenerateToken() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
  }

  static std::string TokenToString(uint64_t token) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << token;
    return oss.str();
  }

  static uint64_t StringToToken(const std::string& str) {
    uint64_t token = 0;
    std::istringstream iss(str);
    iss >> std::hex >> token;
    return token;
  }

 protected:
  uint64_t token_ = 0;

  HANDLE cmd_pipe_ = INVALID_HANDLE_VALUE;
  HANDLE resp_pipe_ = INVALID_HANDLE_VALUE;
  HANDLE log_pipe_ = INVALID_HANDLE_VALUE;
  HANDLE read_event_ = INVALID_HANDLE_VALUE;

  explicit IPC(uint64_t token) : token_(token) {
    read_event_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  }

  virtual ~IPC() {
    if (read_event_ != INVALID_HANDLE_VALUE) {
      CloseHandle(read_event_);
      read_event_ = INVALID_HANDLE_VALUE;
    }
  }

  bool WriteMessage(HANDLE pipe, MessageType message_type,
                    std::span<const std::byte> payload = {}) const {
    MessageHeader header;
    header.message_type = message_type;
    header.token = token_;
    header.payload_size = static_cast<uint32_t>(payload.size());
    header.length = sizeof(MessageHeader) + header.payload_size;

    std::vector<std::byte> buffer(header.length);
    memcpy(buffer.data(), &header, sizeof(MessageHeader));

    if (!payload.empty()) {
      memcpy(buffer.data() + sizeof(MessageHeader), payload.data(),
             payload.size());
    }

    DWORD bytes_written;
    return WriteFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                     &bytes_written, nullptr) &&
           bytes_written == buffer.size();
  }

  std::optional<std::pair<MessageType, nlohmann::json>> ReadMessage(
      HANDLE pipe, int timeout_ms) {
    std::vector<std::byte> msg_buf(kPipeBufferSize);
    DWORD bytes_read;

    OVERLAPPED ol = {};
    ol.hEvent = read_event_;
    ResetEvent(read_event_);

    if (!ReadFile(pipe, msg_buf.data(), static_cast<DWORD>(msg_buf.size()),
                  &bytes_read, &ol)) {
      if (GetLastError() != ERROR_IO_PENDING) return std::nullopt;

      if (WaitForSingleObject(read_event_, timeout_ms) != WAIT_OBJECT_0) {
        CancelIo(pipe);
        return std::nullopt;
      }
      GetOverlappedResult(pipe, &ol, &bytes_read, FALSE);
    }

    if (bytes_read < sizeof(MessageHeader)) return std::nullopt;

    MessageHeader header;
    memcpy(&header, msg_buf.data(), sizeof(MessageHeader));

    if (header.token != token_) return std::nullopt;
    if (bytes_read < sizeof(MessageHeader) + header.payload_size)
      return std::nullopt;

    if (header.message_type == MessageType::kToken &&
        header.payload_size == sizeof(uint32_t)) {
      uint32_t token_value;
      memcpy(&token_value, msg_buf.data() + sizeof(MessageHeader),
             sizeof(uint32_t));
      return std::make_pair(header.message_type,
                            nlohmann::json{{"token", token_value}});
    }

    nlohmann::json payload;
    if (header.payload_size > 0) {
      try {
        auto start = msg_buf.begin() + sizeof(MessageHeader);
        payload = nlohmann::json::parse(start, start + header.payload_size);
      } catch (...) {
        return std::nullopt;
      }
    }

    return std::make_pair(header.message_type, payload);
  }

  std::string GetPipeBaseName() const {
    return "\\\\.\\pipe\\MLPerf_IHV_" + TokenToString(token_);
  }
};

class API_Handler::Server : public API_Handler::IPC {
 public:
  explicit Server(API_Handler::Logger& logger, std::string& ihv_errors)
      : IPC(GenerateToken()), logger_(logger), ihv_errors_(ihv_errors) {
    pipe_name_base_ = GetPipeBaseName();
  }

  ~Server() final { Stop(); }

  bool Start();
  void Stop();
  bool IsRunning() const;

  nlohmann::json SendMessage(MessageType message, const nlohmann::json& payload,
                             int timeout_ms = kIPCTimeoutLong);

  using TokenCallback = std::function<void(uint32_t)>;
  nlohmann::json SendInferCommand(const nlohmann::json& payload,
                                  const TokenCallback& token_callback,
                                  int timeout_ms = kIPCTimeoutLong);

  bool HandleResponse(
      const nlohmann::json& response, const std::string& operation,
      API_Handler::LogLevel level = API_Handler::LogLevel::kError);

  uint64_t GetToken() const { return token_; }

  std::vector<uint8_t> device_list_storage_;
  std::vector<uint32_t> infer_output_buffer_;

 private:
  bool CreatePipes();
  void ClosePipes();
  bool SpawnProcess(const std::string& executable_path);
  bool WaitForClientConnection(int timeout_ms);
  void LogReaderThread();

  API_Handler::Logger& logger_;
  std::string& ihv_errors_;
  std::atomic<bool> running_{false};
  std::thread log_reader_thread_;
  std::atomic<bool> stop_log_reader_{false};

  HANDLE process_handle_ = INVALID_HANDLE_VALUE;
  HANDLE thread_handle_ = INVALID_HANDLE_VALUE;
  HANDLE stdout_pipe_read_ = INVALID_HANDLE_VALUE;
  std::string pipe_name_base_;
};

class API_Handler::Client : public API_Handler::IPC {
 public:
  static int Run(const std::string& token_str);

 private:
  explicit Client(uint64_t token) : IPC(token) {
    write_event_ = CreateEvent(nullptr, TRUE, TRUE, nullptr);
  }
  ~Client() final;

  HANDLE write_event_ = INVALID_HANDLE_VALUE;
  OVERLAPPED write_ol_ = {};
  std::byte token_buffer_[sizeof(MessageHeader) + sizeof(uint32_t)] = {};

  bool Connect();
  void ProcessCommands();
  bool HandleMessage(MessageType message, const nlohmann::json& payload);
  void SendResponse(bool success, const nlohmann::json& data = {});
  void SendLog(API_IHV_LogLevel level, const std::string& message);
  void SendToken(uint32_t token);

  static void TokenCallback(void* object, uint32_t token) {
    static_cast<Client*>(object)->SendToken(token);
  }

  void SendResult(bool result, const std::string& operation = "") {
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
  std::string model_name_;
  std::string model_path_;
  std::string deps_dir_;
  nlohmann::json ep_settings_;
  std::string device_type_;
};

bool API_Handler::Server::Start() {
  if (running_) {
    return true;
  }

  if (!CreatePipes()) {
    logger_(LogLevel::kError,
            "Failed to create named pipes for IHV subprocess");
    return false;
  }

  if (std::string executable_path = utils::GetExecutablePath();
      !SpawnProcess(executable_path)) {
    logger_(LogLevel::kError, "Failed to spawn IHV subprocess");
    ClosePipes();
    return false;
  }

  if (!WaitForClientConnection(kIPCTimeout)) {
    logger_(LogLevel::kError, "IHV subprocess failed to connect");
    Stop();
    return false;
  }

  running_ = true;

  stop_log_reader_ = false;
  log_reader_thread_ = std::thread(&API_Handler::Server::LogReaderThread, this);

  return true;
}

void API_Handler::Server::Stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  stop_log_reader_ = true;
  if (log_reader_thread_.joinable()) {
    log_reader_thread_.join();
  }

  try {
    WriteMessage(cmd_pipe_, MessageType::kShutdown, {});
  } catch (...) {
  }

  if (process_handle_ != INVALID_HANDLE_VALUE) {
    WaitForSingleObject(process_handle_, kIPCTimeout);

    DWORD exitCode;
    if (GetExitCodeProcess(process_handle_, &exitCode) &&
        exitCode == STILL_ACTIVE) {
      TerminateProcess(process_handle_, 1);
    }

    CloseHandle(process_handle_);
    process_handle_ = INVALID_HANDLE_VALUE;
  }

  if (thread_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(thread_handle_);
    thread_handle_ = INVALID_HANDLE_VALUE;
  }

  ClosePipes();
  logger_(LogLevel::kInfo, "IHV subprocess stopped");
}

bool API_Handler::Server::IsRunning() const {
  if (!running_) return false;

  if (process_handle_ == INVALID_HANDLE_VALUE) return false;

  DWORD exitCode;
  if (GetExitCodeProcess(process_handle_, &exitCode)) {
    return exitCode == STILL_ACTIVE;
  }
  return false;
}

bool API_Handler::Server::CreatePipes() {
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

  std::string cmd_pipe_name = pipe_name_base_ + "_cmd";
  cmd_pipe_ = CreateNamedPipeA(
      cmd_pipe_name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, kPipeBufferSize,
      kPipeBufferSize, 0, &sa);

  if (cmd_pipe_ == INVALID_HANDLE_VALUE) {
    return false;
  }

  std::string resp_pipe_name = pipe_name_base_ + "_resp";
  resp_pipe_ = CreateNamedPipeA(
      resp_pipe_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, kPipeBufferSize,
      kPipeBufferSize, 0, &sa);

  if (resp_pipe_ == INVALID_HANDLE_VALUE) {
    CloseHandle(cmd_pipe_);
    cmd_pipe_ = INVALID_HANDLE_VALUE;
    return false;
  }

  std::string log_pipe_name = pipe_name_base_ + "_log";
  log_pipe_ = CreateNamedPipeA(
      log_pipe_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, kPipeBufferSize,
      kPipeBufferSize, 0, &sa);

  if (log_pipe_ == INVALID_HANDLE_VALUE) {
    CloseHandle(cmd_pipe_);
    CloseHandle(resp_pipe_);
    cmd_pipe_ = INVALID_HANDLE_VALUE;
    resp_pipe_ = INVALID_HANDLE_VALUE;
    return false;
  }

  return true;
}

void API_Handler::Server::ClosePipes() {
  if (cmd_pipe_ != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(cmd_pipe_);
    CloseHandle(cmd_pipe_);
    cmd_pipe_ = INVALID_HANDLE_VALUE;
  }
  if (resp_pipe_ != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(resp_pipe_);
    CloseHandle(resp_pipe_);
    resp_pipe_ = INVALID_HANDLE_VALUE;
  }
  if (log_pipe_ != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(log_pipe_);
    CloseHandle(log_pipe_);
    log_pipe_ = INVALID_HANDLE_VALUE;
  }
  if (stdout_pipe_read_ != INVALID_HANDLE_VALUE) {
    CloseHandle(stdout_pipe_read_);
    stdout_pipe_read_ = INVALID_HANDLE_VALUE;
  }
}

bool API_Handler::Server::SpawnProcess(const std::string& executable_path) {
  std::string cmd_line =
      "\"" + executable_path + "\" --ihv-client " + TokenToString(token_);

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdout_write = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&stdout_pipe_read_, &stdout_write, &sa, 0)) {
    logger_(LogLevel::kError, "Failed to create stdout pipe");
    return false;
  }
  SetHandleInformation(stdout_pipe_read_, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = stdout_write;
  si.hStdError = stdout_write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};

  std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
  cmd_buf.push_back('\0');

  BOOL success = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

  CloseHandle(stdout_write);

  if (success) {
    process_handle_ = pi.hProcess;
    thread_handle_ = pi.hThread;
    return true;
  }

  CloseHandle(stdout_pipe_read_);
  stdout_pipe_read_ = INVALID_HANDLE_VALUE;

  DWORD error = GetLastError();
  logger_(LogLevel::kError,
          "CreateProcess failed with error: " + std::to_string(error));
  return false;
}

bool API_Handler::Server::WaitForClientConnection(int timeout_ms) {
  OVERLAPPED ol_cmd = {}, ol_resp = {}, ol_log = {};
  ol_cmd.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  ol_resp.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  ol_log.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

  ConnectNamedPipe(cmd_pipe_, &ol_cmd);
  ConnectNamedPipe(resp_pipe_, &ol_resp);
  ConnectNamedPipe(log_pipe_, &ol_log);

  HANDLE events[] = {ol_cmd.hEvent, ol_resp.hEvent, ol_log.hEvent};

  bool all_connected = true;
  for (int i = 0; i < 3; ++i) {
    DWORD result = WaitForSingleObject(events[i], timeout_ms);
    if (result != WAIT_OBJECT_0) {
      DWORD error = GetLastError();
      if (error != ERROR_PIPE_CONNECTED && error != ERROR_IO_PENDING) {
        all_connected = false;
      }
    }
  }

  CloseHandle(ol_cmd.hEvent);
  CloseHandle(ol_resp.hEvent);
  CloseHandle(ol_log.hEvent);

  return all_connected;
}

nlohmann::json API_Handler::Server::SendMessage(MessageType command,
                                                const nlohmann::json& payload,
                                                int timeout_ms) {
  if (!IsRunning()) {
    return {{"success", false}, {"error", "Subprocess not running"}};
  }

  if (std::string payload_str = payload.dump(); !WriteMessage(
          cmd_pipe_, command, std::as_bytes(std::span(payload_str)))) {
    return {{"success", false}, {"error", "Failed to send command"}};
  }

  auto response = ReadMessage(resp_pipe_, timeout_ms);
  if (!response) {
    return {{"success", false}, {"error", "No response from subprocess"}};
  }

  return response->second;
}

nlohmann::json API_Handler::Server::SendInferCommand(
    const nlohmann::json& payload, const TokenCallback& token_callback,
    int timeout_ms) {
  if (!IsRunning()) {
    return {{"success", false}, {"error", "Subprocess not running"}};
  }

  std::string infer_payload_str = payload.dump();
  if (!WriteMessage(cmd_pipe_, MessageType::kInfer,
                    std::as_bytes(std::span(infer_payload_str)))) {
    return {{"success", false}, {"error", "Failed to send command"}};
  }

  while (true) {
    auto response = ReadMessage(resp_pipe_, timeout_ms);
    if (!response) {
      DWORD exit_code;
      if (GetExitCodeProcess(process_handle_, &exit_code) &&
          exit_code != STILL_ACTIVE) {
        return {{"success", false}, {"error", "Subprocess crashed"}};
      }
      continue;
    }

    if (response->first == MessageType::kToken) {
      if (token_callback) {
        uint32_t token = response->second.value("token", 0u);
        token_callback(token);
      }
    } else if (response->first == MessageType::kResult) {
      return response->second;
    } else {
      return {{"success", false}, {"error", "Unexpected response type"}};
    }
  }
}

bool API_Handler::Server::HandleResponse(const nlohmann::json& response,
                                         const std::string& operation,
                                         API_Handler::LogLevel level) {
  if (!response.value("success", false)) {
    if (response.contains("ihv_errors")) {
      ihv_errors_ = response["ihv_errors"].get<std::string>();
    }
    std::string error =
        response.value("error", "Unknown error in subprocess " + operation);
    logger_(level, error);
    return false;
  }
  return true;
}

void API_Handler::Server::LogReaderThread() {
  while (!stop_log_reader_ && running_) {
    bool had_data = false;

    DWORD bytes_available = 0;
    if (PeekNamedPipe(log_pipe_, nullptr, 0, nullptr, &bytes_available,
                      nullptr) &&
        bytes_available >= sizeof(MessageHeader)) {
      had_data = true;
      std::vector<std::byte> msg_buf(bytes_available);
      DWORD bytes_read;
      if (ReadFile(log_pipe_, msg_buf.data(), bytes_available, &bytes_read,
                   nullptr) &&
          bytes_read >= sizeof(MessageHeader)) {
        MessageHeader header;
        memcpy(&header, msg_buf.data(), sizeof(MessageHeader));
        if (header.token == token_ &&
            header.message_type == MessageType::kLog &&
            header.payload_size > 0 &&
            bytes_read >= sizeof(MessageHeader) + header.payload_size) {
          try {
            auto start = msg_buf.begin() + sizeof(MessageHeader);
            auto payload =
                nlohmann::json::parse(start, start + header.payload_size);
            int level = payload.value("level", 0);
            std::string message = payload.value("message", "");
            logger_(static_cast<API_Handler::LogLevel>(level), message);
          } catch (...) {
          }
        }
      }
    }

    if (stdout_pipe_read_ != INVALID_HANDLE_VALUE &&
        PeekNamedPipe(stdout_pipe_read_, nullptr, 0, nullptr, &bytes_available,
                      nullptr) &&
        bytes_available > 0) {
      had_data = true;
      char buffer[4096];
      DWORD bytes_read;
      if (ReadFile(stdout_pipe_read_, buffer,
                   std::min(bytes_available, (DWORD)sizeof(buffer) - 1),
                   &bytes_read, nullptr) &&
          bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::cout << buffer << std::flush;
      }
    }

    if (!had_data) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

int API_Handler::Client::Run(const std::string& token_str) {
  uint64_t token = StringToToken(token_str);
  if (token == 0) {
    return 1;
  }

  Client client(token);
  if (!client.Connect()) {
    return 2;
  }

  client.ProcessCommands();
  return 0;
}

API_Handler::Client::~Client() {
  if (cmd_pipe_ != INVALID_HANDLE_VALUE) CloseHandle(cmd_pipe_);
  if (resp_pipe_ != INVALID_HANDLE_VALUE) CloseHandle(resp_pipe_);
  if (log_pipe_ != INVALID_HANDLE_VALUE) CloseHandle(log_pipe_);
  if (write_event_ != INVALID_HANDLE_VALUE) CloseHandle(write_event_);
}

bool API_Handler::Client::Connect() {
  std::string pipe_base = GetPipeBaseName();

  std::string cmd_pipe_name = pipe_base + "_cmd";
  cmd_pipe_ = CreateFileA(cmd_pipe_name.c_str(), GENERIC_READ, 0, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (cmd_pipe_ == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(cmd_pipe_, &mode, nullptr, nullptr);

  std::string resp_pipe_name = pipe_base + "_resp";
  resp_pipe_ = CreateFileA(resp_pipe_name.c_str(), GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (resp_pipe_ == INVALID_HANDLE_VALUE) {
    CloseHandle(cmd_pipe_);
    cmd_pipe_ = INVALID_HANDLE_VALUE;
    return false;
  }

  std::string log_pipe_name = pipe_base + "_log";
  log_pipe_ = CreateFileA(log_pipe_name.c_str(), GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, 0, nullptr);

  if (log_pipe_ == INVALID_HANDLE_VALUE) {
    CloseHandle(cmd_pipe_);
    CloseHandle(resp_pipe_);
    cmd_pipe_ = INVALID_HANDLE_VALUE;
    resp_pipe_ = INVALID_HANDLE_VALUE;
    return false;
  }

  return true;
}

void API_Handler::Client::ProcessCommands() {
  while (true) {
    auto msg = ReadMessage(cmd_pipe_, kIPCTimeoutLong);
    if (!msg || !HandleMessage(msg->first, msg->second)) {
      break;
    }
  }
  local_handler_.reset();
}

bool API_Handler::Client::HandleMessage(MessageType message,
                                        const nlohmann::json& payload) {
  auto checkHandler = [this]() {
    if (local_handler_) return true;

    SendResponse(false, {{"error", "Handler not setup"}});
    return false;
  };

  using enum MessageType;
  try {
    switch (message) {
      case kCanBeLoaded: {
        std::string lib_path = payload.value("library_path", "");
        std::stringstream error_ss;

        auto logger = [this, &error_ss](API_Handler::LogLevel level,
                                        std::string msg) {
          if (level == API_Handler::LogLevel::kError ||
              level == API_Handler::LogLevel::kFatal) {
            error_ss << msg;
          }
          SendLog(static_cast<API_IHV_LogLevel>(level), msg);
        };

        auto handler =
            std::make_unique<API_Handler>(lib_path, logger, false, false);

        if (auto error = error_ss.str(); handler->IsLoaded() && error.empty()) {
          SendResponse(true, {});
        } else {
          SendResponse(
              false,
              {{"error", error.empty() ? "Failed to load library" : error}});
        }
        return true;
      }

      case kSetup: {
        library_path_ = payload.value("library_path", "");
        ep_name_ = payload.value("ep_name", "");
        model_name_ = payload.value("model_name", "");
        model_path_ = payload.value("model_path", "");
        deps_dir_ = payload.value("deps_dir", "");
        ep_settings_ = payload.value("ep_settings", nlohmann::json::object());

        auto ipc_logger = [this](API_Handler::LogLevel level, std::string msg) {
          SendLog(static_cast<API_IHV_LogLevel>(level), msg);
        };

        local_handler_ =
            std::make_unique<API_Handler>(library_path_, ipc_logger, false,
                                          false);  // No subprocess recursion!

        if (!local_handler_->IsLoaded()) {
          SendResponse(false, {{"error", "Failed to load IHV library"}});
          local_handler_.reset();
          return true;
        }

        if (!local_handler_->Setup(ep_name_, model_name_, model_path_,
                                   deps_dir_, ep_settings_, device_type_)) {
          SendResponse(false, {{"error", "Failed to setup IHV"},
                               {"ihv_errors", local_handler_->GetIHVErrors()}});
          local_handler_.reset();
          return true;
        }

        SendResponse(true, {{"device_type", device_type_}});
        return true;
      }

      case kEnumerateDevices: {
        if (!checkHandler()) return true;

        API_Handler::DeviceListPtr device_list = nullptr;
        if (!local_handler_->EnumerateDevices(device_list)) {
          SendResponse(false, {{"error", "Failed to enumerate devices"},
                               {"ihv_errors", local_handler_->GetIHVErrors()}});
          return true;
        }

        nlohmann::json devices = nlohmann::json::array();
        for (size_t i = 0; i < device_list->count; ++i) {
          devices.push_back(
              {{"device_id", device_list->device_info_data[i].device_id},
               {"device_name", device_list->device_info_data[i].device_name}});
        }

        SendResponse(true, {{"devices", devices}});
        return true;
      }

      case kInit: {
        if (!checkHandler()) return true;

        std::string model_config = payload.value("model_config", "");
        std::optional<API_IHV_DeviceID_t> device_id;
        if (payload.contains("device_id") && !payload["device_id"].is_null()) {
          device_id = payload["device_id"].get<API_IHV_DeviceID_t>();
        }

        bool result = local_handler_->Init(model_config, device_id);
        SendResponse(result,
                     result ? nlohmann::json{}
                            : nlohmann::json{{"ihv_errors",
                                              local_handler_->GetIHVErrors()}});
        return true;
      }

      case kPrepare: {
        if (!checkHandler()) return true;
        SendResult(local_handler_->Prepare(), "Prepare");
        return true;
      }

      case kInfer: {
        if (!checkHandler()) return true;

        auto input_tokens = payload.value("input", std::vector<uint32_t>{});

        API_IHV_IO_Data_t io_data = {
            input_tokens.data(),
            static_cast<unsigned>(input_tokens.size()),
            nullptr,
            0,
            {API_IHV_CB_Token, this, reinterpret_cast<void*>(TokenCallback)}};

        bool result = local_handler_->Infer(io_data);

        nlohmann::json response_data;
        if (io_data.output && io_data.output_size > 0) {
          const auto* output_ptr = static_cast<const uint32_t*>(io_data.output);
          response_data["output"] = std::vector<uint32_t>(
              output_ptr, output_ptr + io_data.output_size);
        }
        if (!result) {
          response_data["ihv_errors"] = local_handler_->GetIHVErrors();
        }
        SendResponse(result, response_data);
        return true;
      }

      case kReset: {
        if (!checkHandler()) return true;
        SendResult(local_handler_->Reset(), "Reset");
        return true;
      }

      case kDeinit: {
        if (!checkHandler()) return true;
        SendResult(local_handler_->Deinit(), "Deinit");
        return true;
      }

      case kRelease: {
        if (!checkHandler()) return true;
        SendResult(local_handler_->Release(), "Release");
        return true;
      }

      case kPing: {
        SendResponse(true);
        return true;
      }

      case kShutdown: {
        SendResponse(true);
        return false;
      }

      default:
        SendResponse(false, {{"error", "Unknown command"}});
        return true;
    }
  } catch (const std::exception& e) {
    SendResponse(false, {{"error", e.what()}});
    return true;
  }
}

void API_Handler::Client::SendResponse(bool success,
                                       const nlohmann::json& data) {
  nlohmann::json response = data;
  response["success"] = success;
  std::string response_str = response.dump();
  WriteMessage(resp_pipe_, MessageType::kResult,
               std::as_bytes(std::span(response_str)));
}

void API_Handler::Client::SendLog(API_IHV_LogLevel level,
                                  const std::string& message) {
  nlohmann::json payload = {{"level", static_cast<int>(level)},
                            {"message", message}};
  std::string payload_str = payload.dump();
  WriteMessage(log_pipe_, MessageType::kLog,
               std::as_bytes(std::span(payload_str)));
}

void API_Handler::Client::SendToken(uint32_t token_value) {
  WaitForSingleObject(write_event_, INFINITE);

  MessageHeader header;
  header.message_type = MessageType::kToken;
  header.token = token_;
  header.payload_size = sizeof(uint32_t);
  header.length = sizeof(MessageHeader) + header.payload_size;

  memcpy(token_buffer_, &header, sizeof(MessageHeader));
  memcpy(token_buffer_ + sizeof(MessageHeader), &token_value, sizeof(uint32_t));

  write_ol_ = {};
  write_ol_.hEvent = write_event_;
  ResetEvent(write_event_);

  DWORD bytes_written;
  WriteFile(resp_pipe_, token_buffer_, header.length, &bytes_written,
            &write_ol_);
}

int API_Handler::RunSubprocessClient(const std::string& token) {
  utils::SetCurrentDirectory(utils::GetCurrentDirectory());
  return API_Handler::Client::Run(token);
}

#endif  // IHV_SUBPROCESS

API_Handler::API_Handler(const std::string& library_path, Logger logger,
                         bool force_unload_ep
#if IHV_SUBPROCESS
                         ,
                         std::optional<bool> use_subprocess
#endif
                         )
    : library_path_(library_path),
      logger_(logger),
      force_unload_ep_(force_unload_ep)
#if IHV_SUBPROCESS
      ,
      use_subprocess_(use_subprocess.value_or(s_default_subprocess_mode_))
#endif
{

  if (library_path_.empty()) {
    logger(LogLevel::kFatal, "No IHV library path provided.");
    return;
  }

  library_path_directory_ =
      fs::absolute(fs::path(library_path_).parent_path()).string();

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    subprocess_server_ = std::make_unique<Server>(logger_, ihv_errors_);

    if (!subprocess_server_->Start()) {
      logger_(LogLevel::kFatal, "Failed to start IHV subprocess");
      subprocess_server_.reset();
    }
    return;
  }
#endif

#ifdef _WIN32
  WCHAR previousDllDirectory[MAX_PATH] = {0};
  GetDllDirectoryW(MAX_PATH, previousDllDirectory);
  SetDllDirectoryW(
      std::filesystem::path(library_path_directory_).wstring().c_str());
#endif

  library_path_handle_ = utils::AddLibraryPath(library_path_directory_);
  try {
    library_ = std::make_unique<dylib>(library_path_.c_str(),
                                       dylib::no_filename_decorations);
    setup_ = library_->get_function<std::remove_pointer_t<API_IHV_Setup_func>>(
        "API_IHV_Setup");
    enumerate_devices_ = library_->get_function<
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
    logger(LogLevel::kFatal, ex.what());
  }

#ifdef _WIN32
  SetDllDirectoryW(previousDllDirectory[0] != 0 ? previousDllDirectory
                                                : nullptr);
#endif
}

API_Handler::~API_Handler() {
#if IHV_SUBPROCESS
  if (use_subprocess_) {
    if (subprocess_server_) {
      subprocess_server_->Stop();
      subprocess_server_.reset();
    }
    return;
  }
#endif

  try {
    if (ihv_struct_ != nullptr) Release();

    library_.reset();
    if (library_path_handle_.IsValid()) {
      utils::RemoveLibraryPath(library_path_handle_);
    }
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to free IHV library.");
  }

#ifdef WIN32
  if (!force_unload_ep_) {
    return;
  }

  logger_(LogLevel::kInfo, std::string("Unloading EP resources for: ") +
                               library_path_directory_);
  logger_(LogLevel::kInfo,
          std::string("Attempting to force unload DLLs from: ") +
              library_path_directory_);

  std::vector<std::wstring> modulesToUnload;
  HMODULE hMods[1024];
  DWORD cbNeeded;
  HANDLE hProcess = GetCurrentProcess();

  if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
    unsigned int totalModules = cbNeeded / sizeof(HMODULE);
    logger_(LogLevel::kInfo, std::string("Found ") +
                                 std::to_string(totalModules) +
                                 std::string(" total loaded modules"));

    for (unsigned int i = 0; i < totalModules; i++) {
      WCHAR szModPath[MAX_PATH];
      if (GetModuleFileNameExW(hProcess, hMods[i], szModPath,
                               sizeof(szModPath) / sizeof(WCHAR))) {
        std::wstring modPath(szModPath);
        std::filesystem::path depsPathNormalized =
            std::filesystem::path(library_path_directory_);
        std::filesystem::path modPathNormalized =
            std::filesystem::path(modPath);

        std::wstring depsPathWstr = depsPathNormalized.wstring();
        std::wstring modPathWstr = modPathNormalized.wstring();

        std::string modPathStr = modPathNormalized.string();
        if (modPathStr.find("MLPerf") != std::string::npos ||
            modPathStr.find("IHV") != std::string::npos ||
            modPathStr.find("openvino") != std::string::npos ||
            modPathStr.find("onnxruntime") != std::string::npos) {
          logger_(LogLevel::kInfo,
                  std::string("Checking module: ") + modPathStr);
        }

        if (modPathWstr.find(depsPathWstr) != std::wstring::npos) {
          logger_(LogLevel::kInfo,
                  std::string("MATCH - Will unload: ") + modPathStr);
          modulesToUnload.push_back(modPath);
        } else if (modPathStr.find("WindowsApps") != std::string::npos &&
                   modPathStr.find("OpenVINO") != std::string::npos) {
          logger_(LogLevel::kInfo,
                  std::string(
                      "MATCH (WindowsApp OpenVINO) - Will attempt unload: ") +
                      modPathStr);
          modulesToUnload.push_back(modPath);
        }
      }
    }

    logger_(LogLevel::kInfo,
            std::string("Found ") + std::to_string(modulesToUnload.size()) +
                std::string(" modules to unload from EP directory"));

    for (const auto& modPath : modulesToUnload) {
      std::string modPathStr = std::filesystem::path(modPath).string();
      logger_(LogLevel::kInfo,
              std::string("Attempting to unload: ") + modPathStr);

      HMODULE hMod = GetModuleHandleW(modPath.c_str());
      if (hMod != nullptr) {
        int unloadAttempts = 0;
        const int MAX_UNLOAD_ATTEMPTS = 1000;

        while (unloadAttempts < MAX_UNLOAD_ATTEMPTS) {
          if (!FreeLibrary(hMod)) {
            break;
          }
          unloadAttempts++;

          if (GetModuleHandleW(modPath.c_str()) == nullptr) {
            logger_(LogLevel::kInfo,
                    std::string("Successfully unloaded after ") +
                        std::to_string(unloadAttempts) +
                        std::string(" attempts: ") + modPathStr);
            break;
          }
        }

        if (unloadAttempts >= MAX_UNLOAD_ATTEMPTS) {
          logger_(LogLevel::kWarning,
                  std::string("Module still loaded after ") +
                      std::to_string(unloadAttempts) +
                      std::string(" attempts: ") + modPathStr);
        }
      } else {
        logger_(LogLevel::kInfo,
                std::string("Module already unloaded: ") + modPathStr);
      }
    }

    logger_(LogLevel::kInfo, std::string("Completed DLL unload attempts"));
  } else {
    logger_(LogLevel::kWarning,
            std::string("Failed to enumerate process modules"));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
}

bool API_Handler::IsLoaded() const {
#if IHV_SUBPROCESS
  if (use_subprocess_) {
    return subprocess_server_ && subprocess_server_->IsRunning();
  }
#endif
  return library_.get() != nullptr;
}

bool API_Handler::Setup(const std::string& ep_name,
                        const std::string& model_name,
                        const std::string& model_path,
                        const std::string& deps_dir,
                        const nlohmann::json& ep_settings,
                        std::string& device_type_out) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    nlohmann::json payload = {
        {"library_path", library_path_}, {"ep_name", ep_name},
        {"model_name", model_name},      {"model_path", model_path},
        {"deps_dir", deps_dir},          {"ep_settings", ep_settings}};

    auto response = subprocess_server_->SendMessage(IPC::MessageType::kSetup,
                                                    payload, kIPCTimeoutLong);
    if (!subprocess_server_->HandleResponse(response, "setup",
                                            LogLevel::kFatal)) {
      return false;
    }
    device_type_out = response.value("device_type", "Unknown");
    return true;
  }
#endif

  std::string ep_settings_str = ep_settings.dump();

  API_IHV_Setup_t api = {API_IHV_VERSION,
                         this,
                         &Log,
                         APP_VERSION_STRING,
                         ep_name.c_str(),
                         model_name.c_str(),
                         model_path.c_str(),
                         deps_dir.c_str(),
                         ep_settings_str.c_str()};

  try {
    ihv_struct_ = setup_(&api);

    if (!ihv_struct_ || ihv_struct_->device_type == nullptr ||
        strnlen(ihv_struct_->device_type, MAX_DEVICE_TYPE_LENGTH) == 0) {
      logger_(LogLevel::kFatal,
              "Failed to setup IHV library. Invalid device type.");
      return false;
    }

    device_type_out = ihv_struct_->device_type;

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to setup IHV library.");
    return false;
  }
}

bool API_Handler::EnumerateDevices(API_Handler::DeviceListPtr& device_list) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(
        IPC::MessageType::kEnumerateDevices, {}, kIPCTimeoutLong);
    if (!subprocess_server_->HandleResponse(response, "device enumeration",
                                            LogLevel::kFatal)) {
      return false;
    }

    auto devices = response.value("devices", nlohmann::json::array());
    size_t device_count = devices.size();

    size_t alloc_size = sizeof(API_IHV_DeviceList_t) +
                        device_count * sizeof(API_IHV_DeviceInfo_t);
    subprocess_server_->device_list_storage_.resize(alloc_size);

    auto* list = reinterpret_cast<API_IHV_DeviceList_t*>(
        subprocess_server_->device_list_storage_.data());

    list->count = device_count;
    for (size_t i = 0; i < device_count; ++i) {
      list->device_info_data[i].device_id = devices[i].value("device_id", 0);
      std::string name = devices[i].value("device_name", "");
      strncpy_s(list->device_info_data[i].device_name,
                API_IHV_MAX_DEVICE_NAME_LENGTH, name.c_str(), _TRUNCATE);
    }

    device_list = list;
    return true;
  }
#endif

  API_IHV_DeviceEnumeration_t api = {this, &Log, ihv_struct_, nullptr};

  try {
    if (enumerate_devices_(&api) != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kFatal, "IHV unsuccessful device enumeration.");
      return false;
    }
    if (nullptr == api.device_list) {
      logger_(LogLevel::kFatal, "IHV no device list provided.");
      return false;
    }

    device_list = api.device_list;
    return true;
  } catch (const std::exception& e) {
    logger_(LogLevel::kFatal,
            std::string("Failed to call EnumerateDevices() for IHV library. ") +
                e.what());
    return false;
  }
}

bool API_Handler::Init(const std::string& model_config,
                       std::optional<API_IHV_DeviceID_t> device_id) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    nlohmann::json payload = {{"model_config", model_config}};
    if (device_id.has_value()) {
      payload["device_id"] = device_id.value();
    }

    auto response = subprocess_server_->SendMessage(IPC::MessageType::kInit,
                                                    payload, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "init",
                                              LogLevel::kFatal);
  }
#endif

  API_IHV_Init_t api = {this, &Log, ihv_struct_, model_config.c_str(),
                        device_id.has_value() ? &device_id.value() : nullptr};

  try {
    return init_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to initialize IHV library.");
    return false;
  }
}

bool API_Handler::Infer(API_IHV_IO_Data_t& io_data) {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    std::vector<uint32_t> input_tokens;
    if (io_data.input && io_data.input_size > 0) {
      const auto input_ptr = static_cast<const uint32_t*>(io_data.input);
      input_tokens.assign(input_ptr, input_ptr + io_data.input_size);
    }

    nlohmann::json payload = {{"input", input_tokens}};

    Server::TokenCallback token_callback = nullptr;
    if (io_data.callback.type == API_IHV_CB_Token &&
        io_data.callback.function != nullptr &&
        io_data.callback.object != nullptr) {
      auto original_callback = reinterpret_cast<void (*)(void*, uint32_t)>(
          io_data.callback.function);
      void* original_object = io_data.callback.object;
      token_callback = [original_callback, original_object](uint32_t token) {
        original_callback(original_object, token);
      };
    }

    auto response =
        subprocess_server_->SendInferCommand(payload, token_callback);
    if (!subprocess_server_->HandleResponse(response, "inference")) {
      return false;
    }

    if (response.contains("output")) {
      subprocess_server_->infer_output_buffer_ =
          response["output"].get<std::vector<uint32_t>>();
      io_data.output = subprocess_server_->infer_output_buffer_.data();
      io_data.output_size = static_cast<unsigned>(
          subprocess_server_->infer_output_buffer_.size());
    }
    return true;
  }
#endif

  API_IHV_Infer_t api = {this, &Log, ihv_struct_, &io_data};

  try {
    int ret = infer_(&api);

    if (ret != API_IHV_RETURN_SUCCESS) {
      logger_(LogLevel::kError,
              "IHV library inference returned error: " + std::to_string(ret));
      return false;
    }

    if ((API_IHV_Callback_Type::API_IHV_CB_None ==
         api.io_data->callback.type) &&
        (api.io_data->output_size == 0 || api.io_data->output == nullptr)) {
      logger_(LogLevel::kError,
              "IHV library inference returned no output data.");
      return false;
    }

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to call Infer() for IHV library.");
    return false;
  }
}

bool API_Handler::Deinit() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kDeinit,
                                                    {}, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "deinit");
  }
#endif

  API_IHV_Deinit_t api = {this, &Log, ihv_struct_};

  try {
    return deinit_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to deinitialize IHV library.");
    return false;
  }
}

bool API_Handler::Release() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kRelease,
                                                    {}, kIPCTimeoutLong);
    return response.value("success", false);
  }
#endif

  if (ihv_struct_ == nullptr) {
    logger_(LogLevel::kFatal, "Invalid IHV library struct.");
    return false;
  }

  API_IHV_Release_t api = {this, &Log, ihv_struct_};

  try {
    release_(&api);

    ihv_struct_ = nullptr;

    return true;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to release IHV library.");
    return false;
  }
}

bool API_Handler::Prepare() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kPrepare,
                                                    {}, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "prepare");
  }
#endif

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return prepare_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to call Prepare() for IHV library.");
    return false;
  }
}

bool API_Handler::Reset() {
  if (!IsLoaded()) {
    logger_(LogLevel::kFatal, "IHV library is not loaded.");
    return false;
  }

#if IHV_SUBPROCESS
  if (use_subprocess_) {
    auto response = subprocess_server_->SendMessage(IPC::MessageType::kReset,
                                                    {}, kIPCTimeoutLong);
    return subprocess_server_->HandleResponse(response, "reset");
  }
#endif

  API_IHV_Simple_t api = {this, &Log, ihv_struct_};

  try {
    return reset_(&api) == API_IHV_RETURN_SUCCESS;
  } catch (...) {
    logger_(LogLevel::kFatal, "Failed to call Reset() for IHV library.");
    return false;
  }
}

std::string API_Handler::CanBeLoaded(const std::string& library_path
#if IHV_SUBPROCESS
                                     ,
                                     std::optional<bool> use_subprocess
#endif
) {
  std::stringstream ss;
  std::string ihv_errors;

  Logger logger = [&ss](LogLevel level, std::string message) {
    if (level == LogLevel::kError || level == LogLevel::kFatal) {
      ss << message;
      // Ignore other log levels, if result is non emtpy it means failure.
    }
  };

#if IHV_SUBPROCESS
  if (use_subprocess.value_or(s_default_subprocess_mode_)) {
    Server server(logger, ihv_errors);
    if (!server.Start()) {
      return "Failed to start subprocess";
    }

    nlohmann::json payload = {{"library_path", library_path}};
    auto response = server.SendMessage(IPC::MessageType::kCanBeLoaded, payload,
                                       kIPCTimeout);

    server.SendMessage(IPC::MessageType::kShutdown, {}, kIPCTimeout);
    server.Stop();

    if (response.value("success", false)) {
      return "";
    }
    return response.value("error", "Failed to load in subprocess");
  }
#endif

  auto api_handler = std::make_unique<API_Handler>(library_path, logger, false
#if IHV_SUBPROCESS
                                                   ,
                                                   false
#endif
  );

  return ss.str();
}

API_Handler::DeviceList API_Handler::EnumerateDevices(
    const std::string& library_path, const std::string& ep_name,
    const std::string& model_name, const nlohmann::json& ep_settings,
    std::string& error, std::string& log
#if IHV_SUBPROCESS
    ,
    std::optional<bool> use_subprocess
#endif
) {
  std::stringstream ss;
  std::stringstream ss_err;

  Logger logger = [&ss, &ss_err](LogLevel level, std::string message) {
    using enum LogLevel;

    switch (level) {
      case kInfo:
        ss << "INFO: ";
        ss << message << std::endl;
        break;
      case kWarning:
        ss << "WARNING: ";
        ss << message << std::endl;
        break;
      case kError:
        ss_err << "ERROR: ";
        ss_err << message << std::endl;
        break;
      case kFatal:
        ss_err << "FATAL: ";
        ss_err << message << std::endl;
        break;
    }
  };

  auto api_handler = std::make_unique<API_Handler>(library_path, logger, false
#if IHV_SUBPROCESS
                                                   ,
                                                   use_subprocess
#endif
  );

  auto deps_dir = fs::absolute(fs::path(library_path).parent_path()).string();

  std::string device_type_out;
  if (!api_handler->Setup(ep_name, model_name, "", deps_dir, ep_settings,
                          device_type_out)) {
    log = ss.str();
    error = ss_err.str();
    return {};
  }

  API_Handler::DeviceListPtr device_list;

  if (!api_handler->EnumerateDevices(device_list)) {
    log = ss.str();
    error = ss_err.str();
    return {};
  }

  DeviceList devices;
  for (size_t i = 0; i < device_list->count; ++i) {
    auto device_id = device_list->device_info_data[i].device_id;
    auto device_name = device_list->device_info_data[i].device_name;
    if (device_name == nullptr) {
      device_name = "";
    }
    DeviceInfo device_info = {device_id, device_name};
    devices.emplace_back(device_info);
  }

  log = ss.str();
  error = ss_err.str();

  return devices;
}

void API_Handler::Log(void* context, API_IHV_LogLevel level,
                      const char* message) {
  auto* obj = static_cast<API_Handler*>(context);

  if (obj == nullptr) {
    throw std::runtime_error("API_Handler object is null.");
  }

  switch (level) {
    case API_IHV_INFO:
      obj->logger_(LogLevel::kInfo, message);
      break;
    case API_IHV_WARNING:
      obj->logger_(LogLevel::kWarning, message);
      break;
    case API_IHV_ERROR:
      obj->logger_(LogLevel::kError, message);
      obj->ihv_errors_ += message;
      obj->ihv_errors_ += "\n";
      break;
    case API_IHV_FATAL:
    default:
      obj->logger_(LogLevel::kFatal, message);
      obj->ihv_errors_ += message;
      obj->ihv_errors_ += "\n";
      break;
  }
}

}  // namespace cil
