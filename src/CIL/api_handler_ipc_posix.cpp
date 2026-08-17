#include "api_handler_ipc.h"

#if IHV_SUBPROCESS

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

extern char** environ;

namespace cil {

namespace {

// The child receives its three IPC sockets at these fixed descriptors,
// placed there by posix_spawn file actions. Numbers are deliberately high so
// they don't collide with FDs the parent had open at fork time (which the
// child inherits and which would otherwise be clobbered by dup2, leaving the
// real socket fds dangling and the parent staring at an immediate POLLHUP).
constexpr int kChildCmdFd = 100;
constexpr int kChildRespFd = 101;
constexpr int kChildLogFd = 102;

// Reads exactly `size` bytes from `fd`, honouring a per-read timeout. Returns
// false on timeout, EOF, or error.
bool ReadExact(int fd, std::byte* buf, size_t size, int timeout_ms) {
  size_t got = 0;
  while (got < size) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (pr == 0) return false;

    ssize_t n = read(fd, buf + got, size - got);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

// Writes all `size` bytes to `fd`. Returns false on error.
bool WriteExact(int fd, const std::byte* buf, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = write(fd, buf + sent, size - sent);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

// Bounded write for non-blocking fds: completes within `timeout_ms` total wall
// time or returns false. Partial writes are possible on timeout; callers must
// close the channel on failure to avoid desyncing the length-framed stream.
bool WriteBounded(int fd, const std::byte* buf, size_t size, int timeout_ms) {
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = write(fd, buf + sent, size - sent);
    if (n >= 0) {
      sent += static_cast<size_t>(n);
      continue;
    }
    if (errno == EINTR) continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK) return false;

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - clock::now())
                               .count();
    if (remaining <= 0) return false;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int pr = poll(&pfd, 1, static_cast<int>(remaining));
    if (pr <= 0) return false;
    if (!(pfd.revents & POLLOUT)) return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Pimpl - POSIX transport state.
// ---------------------------------------------------------------------------

struct API_Handler::IPC::Impl {
  // Local ends of the three channels (parent ends on the server, the
  // inherited descriptors on the client).
  int cmd_fd = -1;
  int resp_fd = -1;
  int log_fd = -1;

  // Server-side only: child ends held between CreatePipes() and SpawnProcess().
  int child_cmd_fd = -1;
  int child_resp_fd = -1;
  int child_log_fd = -1;
  pid_t child_pid = -1;

  int Resolve(Channel channel) const {
    switch (channel) {
      case Channel::kResponse:
        return resp_fd;
      case Channel::kLog:
        return log_fd;
      case Channel::kCommand:
      default:
        return cmd_fd;
    }
  }
};

API_Handler::IPC::IPC(uint64_t token)
    : token_(token), impl_(std::make_unique<Impl>()) {
  // A write to a closed peer must surface as EPIPE, not a fatal signal.
  signal(SIGPIPE, SIG_IGN);
}

API_Handler::IPC::~IPC() {
  if (impl_->cmd_fd >= 0) close(impl_->cmd_fd);
  if (impl_->resp_fd >= 0) close(impl_->resp_fd);
  if (impl_->log_fd >= 0) close(impl_->log_fd);
  if (impl_->child_cmd_fd >= 0) close(impl_->child_cmd_fd);
  if (impl_->child_resp_fd >= 0) close(impl_->child_resp_fd);
  if (impl_->child_log_fd >= 0) close(impl_->child_log_fd);
}

// ---------------------------------------------------------------------------
// IPC - message transport (Unix-domain stream sockets, length-framed).
// ---------------------------------------------------------------------------

bool API_Handler::IPC::WriteMessage(Channel channel, MessageType message_type,
                                    std::span<const std::byte> payload) const {
  int fd = impl_->Resolve(channel);
  if (fd < 0) return false;

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

  // Log channel is best-effort and bounded; close on failure to drop further
  // writes rather than desync the stream after a partial write.
  if (channel == Channel::kLog) {
    if (WriteBounded(fd, buffer.data(), buffer.size(), kIPCTimeoutLong)) {
      return true;
    }
    if (impl_->log_fd >= 0) {
      close(impl_->log_fd);
      impl_->log_fd = -1;
    }
    return false;
  }

  return WriteExact(fd, buffer.data(), buffer.size());
}

std::optional<std::pair<API_Handler::IPC::MessageType, nlohmann::json>>
API_Handler::IPC::ReadMessage(Channel channel, int timeout_ms) {
  int fd = impl_->Resolve(channel);
  if (fd < 0) return std::nullopt;

  // Stream sockets carry no message boundaries: read the fixed header first,
  // then the payload whose size the header declares.
  MessageHeader header;
  if (!ReadExact(fd, reinterpret_cast<std::byte*>(&header), sizeof(header),
                 timeout_ms)) {
    return std::nullopt;
  }

  if (header.length < sizeof(MessageHeader)) return std::nullopt;

  std::vector<std::byte> msg_buf(header.length);
  memcpy(msg_buf.data(), &header, sizeof(MessageHeader));

  if (header.payload_size > 0) {
    if (!ReadExact(fd, msg_buf.data() + sizeof(MessageHeader),
                   header.payload_size, timeout_ms)) {
      return std::nullopt;
    }
  }

  return ParseMessage(msg_buf.data(), msg_buf.size(), token_);
}

// ---------------------------------------------------------------------------
// Server - POSIX transport.
// ---------------------------------------------------------------------------

bool API_Handler::Server::CreatePipes() {
  auto make_channel = [](int& local_fd, int& child_fd) -> bool {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;
    local_fd = sv[0];
    child_fd = sv[1];
    return true;
  };

  if (!make_channel(impl_->cmd_fd, impl_->child_cmd_fd)) {
    return false;
  }
  if (!make_channel(impl_->resp_fd, impl_->child_resp_fd)) {
    ClosePipes();
    return false;
  }
  if (!make_channel(impl_->log_fd, impl_->child_log_fd)) {
    ClosePipes();
    return false;
  }

  // The AF_UNIX SOCK_STREAM default (~8 KiB) is too small for log bursts.
  auto set_buf = [](int fd) {
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kPipeBufferSize,
               sizeof(kPipeBufferSize));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kPipeBufferSize,
               sizeof(kPipeBufferSize));
  };
  set_buf(impl_->cmd_fd);
  set_buf(impl_->child_cmd_fd);
  set_buf(impl_->resp_fd);
  set_buf(impl_->child_resp_fd);
  set_buf(impl_->log_fd);
  set_buf(impl_->child_log_fd);

  // The parent ends must not leak into the spawned child.
  fcntl(impl_->cmd_fd, F_SETFD, FD_CLOEXEC);
  fcntl(impl_->resp_fd, F_SETFD, FD_CLOEXEC);
  fcntl(impl_->log_fd, F_SETFD, FD_CLOEXEC);

  return true;
}

void API_Handler::Server::ClosePipes() {
  if (impl_->cmd_fd >= 0) {
    close(impl_->cmd_fd);
    impl_->cmd_fd = -1;
  }
  if (impl_->resp_fd >= 0) {
    close(impl_->resp_fd);
    impl_->resp_fd = -1;
  }
  if (impl_->log_fd >= 0) {
    close(impl_->log_fd);
    impl_->log_fd = -1;
  }
  if (impl_->child_cmd_fd >= 0) {
    close(impl_->child_cmd_fd);
    impl_->child_cmd_fd = -1;
  }
  if (impl_->child_resp_fd >= 0) {
    close(impl_->child_resp_fd);
    impl_->child_resp_fd = -1;
  }
  if (impl_->child_log_fd >= 0) {
    close(impl_->child_log_fd);
    impl_->child_log_fd = -1;
  }
}

bool API_Handler::Server::SpawnProcess(const std::string& executable_path) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  // Place the child ends of the three channels at fixed descriptors the
  // client knows to adopt.
  posix_spawn_file_actions_adddup2(&actions, impl_->child_cmd_fd, kChildCmdFd);
  posix_spawn_file_actions_adddup2(&actions, impl_->child_resp_fd,
                                   kChildRespFd);
  posix_spawn_file_actions_adddup2(&actions, impl_->child_log_fd, kChildLogFd);

  std::string token_str = TokenToString(token_);
  std::string exe = executable_path;
  std::string client_flag = "--ihv-client";

  std::vector<char*> argv;
  argv.push_back(exe.data());
  argv.push_back(client_flag.data());
  argv.push_back(token_str.data());
  argv.push_back(nullptr);

  pid_t pid = -1;
  int rc = posix_spawn(&pid, executable_path.c_str(), &actions, nullptr,
                       argv.data(), environ);

  posix_spawn_file_actions_destroy(&actions);

  // The child ends are now owned by the spawned process.
  if (impl_->child_cmd_fd >= 0) {
    close(impl_->child_cmd_fd);
    impl_->child_cmd_fd = -1;
  }
  if (impl_->child_resp_fd >= 0) {
    close(impl_->child_resp_fd);
    impl_->child_resp_fd = -1;
  }
  if (impl_->child_log_fd >= 0) {
    close(impl_->child_log_fd);
    impl_->child_log_fd = -1;
  }

  if (rc != 0) {
    logger_(LogLevel::kError,
            "posix_spawn failed: " + std::string(strerror(rc)));
    return false;
  }

  impl_->child_pid = pid;
  return true;
}

bool API_Handler::Server::WaitForClientConnection(int /*timeout_ms*/) {
  // socketpair channels are connected the moment they are created.
  return true;
}

bool API_Handler::Server::IsRunning() const {
  if (!running_) return false;
  if (impl_->child_pid <= 0) return false;

  int status;
  pid_t r = waitpid(impl_->child_pid, &status, WNOHANG);
  return r == 0;  // 0 => still running
}

void API_Handler::Server::StopProcess() {
  if (impl_->child_pid <= 0) return;

  // Give the child time to exit on its own after the kShutdown command.
  for (int i = 0; i < 50; ++i) {
    int status;
    if (waitpid(impl_->child_pid, &status, WNOHANG) != 0) {
      impl_->child_pid = -1;
      return;
    }
    usleep(100 * 1000);
  }

  kill(impl_->child_pid, SIGTERM);
  for (int i = 0; i < 20; ++i) {
    int status;
    if (waitpid(impl_->child_pid, &status, WNOHANG) != 0) {
      impl_->child_pid = -1;
      return;
    }
    usleep(100 * 1000);
  }

  kill(impl_->child_pid, SIGKILL);
  int status;
  waitpid(impl_->child_pid, &status, 0);
  impl_->child_pid = -1;
}

void API_Handler::Server::LogReaderThread() {
  while (!stop_log_reader_ && running_) {
    struct pollfd pfd;
    pfd.fd = impl_->log_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, 100);
    if (pr <= 0) continue;
    if (!(pfd.revents & POLLIN)) continue;

    MessageHeader header;
    if (!ReadExact(impl_->log_fd, reinterpret_cast<std::byte*>(&header),
                   sizeof(header), kIPCTimeout)) {
      continue;
    }
    if (header.length < sizeof(MessageHeader)) continue;

    std::vector<std::byte> buf(header.length);
    memcpy(buf.data(), &header, sizeof(MessageHeader));
    if (header.payload_size > 0 &&
        !ReadExact(impl_->log_fd, buf.data() + sizeof(MessageHeader),
                   header.payload_size, kIPCTimeout)) {
      continue;
    }

    HandleLogBytes(buf.data(), buf.size());
  }
}

// ---------------------------------------------------------------------------
// Client - POSIX transport.
// ---------------------------------------------------------------------------

bool API_Handler::Client::Connect() {
  // The parent placed the three channels at fixed descriptors via the
  // posix_spawn file actions; adopt them.
  if (fcntl(kChildCmdFd, F_GETFD) < 0 || fcntl(kChildRespFd, F_GETFD) < 0 ||
      fcntl(kChildLogFd, F_GETFD) < 0) {
    return false;
  }

  impl_->cmd_fd = kChildCmdFd;
  impl_->resp_fd = kChildRespFd;
  impl_->log_fd = kChildLogFd;

  // O_NONBLOCK so a full log buffer surfaces as EAGAIN for WriteBounded.
  if (int fl = fcntl(impl_->log_fd, F_GETFL, 0); fl >= 0) {
    fcntl(impl_->log_fd, F_SETFL, fl | O_NONBLOCK);
  }
  return true;
}

}  // namespace cil

#endif  // IHV_SUBPROCESS
