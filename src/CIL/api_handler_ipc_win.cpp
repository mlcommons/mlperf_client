#include "api_handler_ipc.h"

#if IHV_SUBPROCESS

#include <windows.h>

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace cil {


// ---------------------------------------------------------------------------
// Pimpl - Windows transport state.
// ---------------------------------------------------------------------------

struct API_Handler::IPC::Impl {
  HANDLE cmd_pipe = INVALID_HANDLE_VALUE;
  HANDLE resp_pipe = INVALID_HANDLE_VALUE;
  HANDLE log_pipe = INVALID_HANDLE_VALUE;
  HANDLE read_event = INVALID_HANDLE_VALUE;

  // Server-side only.
  HANDLE process_handle = INVALID_HANDLE_VALUE;
  HANDLE thread_handle = INVALID_HANDLE_VALUE;
  HANDLE stdout_pipe_read = INVALID_HANDLE_VALUE;

  HANDLE Resolve(Channel channel) const {
    using enum Channel;
    switch (channel) {
      case kResponse:
        return resp_pipe;
      case kLog:
        return log_pipe;
      case kCommand:
      default:
        return cmd_pipe;
    }
  }
};

API_Handler::IPC::IPC(uint64_t token)
    : token_(token), impl_(std::make_unique<Impl>()) {
  impl_->read_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

API_Handler::IPC::~IPC() {
  if (impl_->read_event != INVALID_HANDLE_VALUE) CloseHandle(impl_->read_event);
  if (impl_->cmd_pipe != INVALID_HANDLE_VALUE) CloseHandle(impl_->cmd_pipe);
  if (impl_->resp_pipe != INVALID_HANDLE_VALUE) CloseHandle(impl_->resp_pipe);
  if (impl_->log_pipe != INVALID_HANDLE_VALUE) CloseHandle(impl_->log_pipe);
  if (impl_->process_handle != INVALID_HANDLE_VALUE)
    CloseHandle(impl_->process_handle);
  if (impl_->thread_handle != INVALID_HANDLE_VALUE)
    CloseHandle(impl_->thread_handle);
  if (impl_->stdout_pipe_read != INVALID_HANDLE_VALUE)
    CloseHandle(impl_->stdout_pipe_read);
}

// ---------------------------------------------------------------------------
// IPC - message transport (named pipes, message mode).
// ---------------------------------------------------------------------------

bool API_Handler::IPC::WriteMessage(Channel channel, MessageType message_type,
                                    std::span<const std::byte> payload) const {
  HANDLE pipe = impl_->Resolve(channel);

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

  // Overlapped write + wait: a message larger than the pipe buffer must block
  // until the reader drains it, which a NULL-lpOverlapped write mishandles.
  OVERLAPPED ol = {};
  ol.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  if (ol.hEvent == nullptr) return false;

  DWORD bytes_written = 0;
  BOOL ok = WriteFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &bytes_written, &ol);
  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    if (WaitForSingleObject(ol.hEvent, kIPCTimeoutLong) == WAIT_OBJECT_0) {
      ok = GetOverlappedResult(pipe, &ol, &bytes_written, FALSE);
    } else {
      CancelIoEx(pipe, &ol);
    }
  }

  CloseHandle(ol.hEvent);
  return ok && bytes_written == buffer.size();
}

std::optional<std::pair<API_Handler::IPC::MessageType, nlohmann::json>>
API_Handler::IPC::ReadMessage(Channel channel, int timeout_ms) {
  HANDLE pipe = impl_->Resolve(channel);

  // Byte-mode framing: read the fixed header, then exactly header.length
  // bytes. (Message-mode pipes fragment multi-MB messages unpredictably.)
  auto read_exact = [&](std::byte* dst, size_t count) {
    size_t got = 0;
    while (got < count) {
      DWORD bytes_read = 0;
      OVERLAPPED ol = {};
      ol.hEvent = impl_->read_event;
      ResetEvent(impl_->read_event);

      BOOL ok = ReadFile(pipe, dst + got, static_cast<DWORD>(count - got),
                         &bytes_read, &ol);
      if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(impl_->read_event, timeout_ms) !=
            WAIT_OBJECT_0) {
          CancelIo(pipe);
          return false;
        }
        ok = GetOverlappedResult(pipe, &ol, &bytes_read, FALSE);
      }
      if (!ok || bytes_read == 0) return false;
      got += bytes_read;
    }
    return true;
  };

  std::vector<std::byte> msg_buf(sizeof(MessageHeader));
  if (!read_exact(msg_buf.data(), sizeof(MessageHeader))) return std::nullopt;

  MessageHeader header;
  memcpy(&header, msg_buf.data(), sizeof(MessageHeader));
  if (header.length < sizeof(MessageHeader)) return std::nullopt;

  msg_buf.resize(header.length);
  if (!read_exact(msg_buf.data() + sizeof(MessageHeader),
                  header.length - sizeof(MessageHeader)))
    return std::nullopt;

  return ParseMessage(msg_buf.data(), header.length, token_);
}

// ---------------------------------------------------------------------------
// Server - Windows transport.
// ---------------------------------------------------------------------------

bool API_Handler::Server::CreatePipes() {
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  std::string pipe_name_base =
      R"(\\.\pipe\MLPerf_IHV_)" + TokenToString(token_);

  std::string cmd_pipe_name = pipe_name_base + "_cmd";
  impl_->cmd_pipe = CreateNamedPipeA(
      cmd_pipe_name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, kPipeBufferSize,
      kPipeBufferSize, 0, &sa);

  if (impl_->cmd_pipe == INVALID_HANDLE_VALUE) {
    return false;
  }

  std::string resp_pipe_name = pipe_name_base + "_resp";
  impl_->resp_pipe = CreateNamedPipeA(
      resp_pipe_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, kPipeBufferSize,
      kPipeBufferSize, 0, &sa);

  if (impl_->resp_pipe == INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->cmd_pipe);
    impl_->cmd_pipe = INVALID_HANDLE_VALUE;
    return false;
  }

  std::string log_pipe_name = pipe_name_base + "_log";
  impl_->log_pipe = CreateNamedPipeA(
      log_pipe_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, kPipeBufferSize,
      kPipeBufferSize, 0, &sa);

  if (impl_->log_pipe == INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->cmd_pipe);
    CloseHandle(impl_->resp_pipe);
    impl_->cmd_pipe = INVALID_HANDLE_VALUE;
    impl_->resp_pipe = INVALID_HANDLE_VALUE;
    return false;
  }

  return true;
}

void API_Handler::Server::ClosePipes() {
  if (impl_->cmd_pipe != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(impl_->cmd_pipe);
    CloseHandle(impl_->cmd_pipe);
    impl_->cmd_pipe = INVALID_HANDLE_VALUE;
  }
  if (impl_->resp_pipe != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(impl_->resp_pipe);
    CloseHandle(impl_->resp_pipe);
    impl_->resp_pipe = INVALID_HANDLE_VALUE;
  }
  if (impl_->log_pipe != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(impl_->log_pipe);
    CloseHandle(impl_->log_pipe);
    impl_->log_pipe = INVALID_HANDLE_VALUE;
  }
  if (impl_->stdout_pipe_read != INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->stdout_pipe_read);
    impl_->stdout_pipe_read = INVALID_HANDLE_VALUE;
  }
  // Closing the read handle unblocks StdoutReaderThread's pending ReadFile.
  if (stdout_reader_thread_.joinable()) stdout_reader_thread_.join();
}

bool API_Handler::Server::SpawnProcess(const std::string& executable_path) {
  std::string cmd_line =
      "\"" + executable_path + "\" --ihv-client " + TokenToString(token_);

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdout_write = INVALID_HANDLE_VALUE;
  // Large buffer; StdoutReaderThread drains it with blocking reads.
  if (!CreatePipe(&impl_->stdout_pipe_read, &stdout_write, &sa, 1 << 20)) {
    logger_(LogLevel::kError, "Failed to create stdout pipe");
    return false;
  }
  SetHandleInformation(impl_->stdout_pipe_read, HANDLE_FLAG_INHERIT, 0);

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
    impl_->process_handle = pi.hProcess;
    impl_->thread_handle = pi.hThread;
    stdout_reader_thread_ =
        std::thread(&API_Handler::Server::StdoutReaderThread, this);
    return true;
  }

  CloseHandle(impl_->stdout_pipe_read);
  impl_->stdout_pipe_read = INVALID_HANDLE_VALUE;

  DWORD error = GetLastError();
  logger_(LogLevel::kError,
          "CreateProcess failed with error: " + std::to_string(error));
  return false;
}

bool API_Handler::Server::WaitForClientConnection(int timeout_ms) {
  OVERLAPPED ol_cmd = {};
  OVERLAPPED ol_resp = {};
  OVERLAPPED ol_log = {};
  ol_cmd.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  ol_resp.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  ol_log.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

  ConnectNamedPipe(impl_->cmd_pipe, &ol_cmd);
  ConnectNamedPipe(impl_->resp_pipe, &ol_resp);
  ConnectNamedPipe(impl_->log_pipe, &ol_log);

  std::array<HANDLE, 3> events = {ol_cmd.hEvent, ol_resp.hEvent,
                                  ol_log.hEvent};

  bool all_connected = true;
  for (HANDLE event : events) {
    DWORD result = WaitForSingleObject(event, timeout_ms);
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

bool API_Handler::Server::IsRunning() const {
  if (!running_) return false;

  if (impl_->process_handle == INVALID_HANDLE_VALUE) return false;

  if (DWORD exitCode; GetExitCodeProcess(impl_->process_handle, &exitCode)) {
    return exitCode == STILL_ACTIVE;
  }
  return false;
}

void API_Handler::Server::StopProcess() {
  if (impl_->process_handle != INVALID_HANDLE_VALUE) {
    WaitForSingleObject(impl_->process_handle, kIPCTimeout);

    if (DWORD exitCode;
        GetExitCodeProcess(impl_->process_handle, &exitCode) &&
        exitCode == STILL_ACTIVE) {
      TerminateProcess(impl_->process_handle, 1);
    }

    CloseHandle(impl_->process_handle);
    impl_->process_handle = INVALID_HANDLE_VALUE;
  }

  if (impl_->thread_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->thread_handle);
    impl_->thread_handle = INVALID_HANDLE_VALUE;
  }
}

void API_Handler::Server::LogReaderThread() {
  while (!stop_log_reader_ && running_) {
    std::array<std::byte, sizeof(MessageHeader)> header_buf;
    DWORD peeked = 0;
    DWORD available = 0;
    // Byte-mode pipe: peek the header, then consume the framed message only
    // once all `length` bytes are buffered.
    if (PeekNamedPipe(impl_->log_pipe, header_buf.data(),
                      static_cast<DWORD>(sizeof(MessageHeader)), &peeked,
                      &available, nullptr) &&
        peeked == sizeof(MessageHeader)) {
      MessageHeader header;
      memcpy(&header, header_buf.data(), sizeof(MessageHeader));
      if (header.length >= sizeof(MessageHeader) &&
          available >= header.length) {
        std::vector<std::byte> msg_buf(header.length);
        if (DWORD bytes_read = 0;
            ReadFile(impl_->log_pipe, msg_buf.data(), header.length,
                     &bytes_read, nullptr) &&
            bytes_read == header.length) {
          HandleLogBytes(msg_buf.data(), bytes_read);
        }
        continue;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

void API_Handler::Server::StdoutReaderThread() {
  // Blocking reads drain the child's stdout/stderr continuously, so a chatty
  // EP cannot fill the pipe buffer and stall the subprocess mid-call.
  char buffer[8192];
  DWORD bytes_read = 0;
  while (impl_->stdout_pipe_read != INVALID_HANDLE_VALUE &&
         ReadFile(impl_->stdout_pipe_read, buffer,
                  static_cast<DWORD>(sizeof(buffer) - 1), &bytes_read,
                  nullptr) &&
         bytes_read > 0) {
    buffer[bytes_read] = '\0';
    std::cout << buffer << std::flush;
  }
}

// ---------------------------------------------------------------------------
// Client - Windows transport.
// ---------------------------------------------------------------------------

bool API_Handler::Client::Connect() {
  std::string pipe_base = R"(\\.\pipe\MLPerf_IHV_)" + TokenToString(token_);

  std::string cmd_pipe_name = pipe_base + "_cmd";
  impl_->cmd_pipe = CreateFileA(cmd_pipe_name.c_str(), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (impl_->cmd_pipe == INVALID_HANDLE_VALUE) {
    return false;
  }

  std::string resp_pipe_name = pipe_base + "_resp";
  impl_->resp_pipe =
      CreateFileA(resp_pipe_name.c_str(), GENERIC_WRITE, 0, nullptr,
                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (impl_->resp_pipe == INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->cmd_pipe);
    impl_->cmd_pipe = INVALID_HANDLE_VALUE;
    return false;
  }

  std::string log_pipe_name = pipe_base + "_log";
  impl_->log_pipe = CreateFileA(log_pipe_name.c_str(), GENERIC_WRITE, 0,
                                nullptr, OPEN_EXISTING, 0, nullptr);

  if (impl_->log_pipe == INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->cmd_pipe);
    CloseHandle(impl_->resp_pipe);
    impl_->cmd_pipe = INVALID_HANDLE_VALUE;
    impl_->resp_pipe = INVALID_HANDLE_VALUE;
    return false;
  }

  return true;
}

}  // namespace cil

#endif  // IHV_SUBPROCESS
