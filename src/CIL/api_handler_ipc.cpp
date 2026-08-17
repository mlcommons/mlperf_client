#include "api_handler_ipc.h"

#if IHV_SUBPROCESS

#include <array>
#include <cstring>
#include <iomanip>
#include <memory>
#include <random>
#include <span>
#include <sstream>
#include <vector>

#include "utils.h"

namespace cil {

// ---------------------------------------------------------------------------
// IPC - protocol helpers (platform-agnostic).
// ---------------------------------------------------------------------------

uint64_t API_Handler::IPC::GenerateToken() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis;
  return dis(gen);
}

std::string API_Handler::IPC::TokenToString(uint64_t token) {
  std::ostringstream oss;
  oss << std::hex << std::setw(16) << std::setfill('0') << token;
  return oss.str();
}

uint64_t API_Handler::IPC::StringToToken(const std::string& str) {
  uint64_t token = 0;
  std::istringstream iss(str);
  iss >> std::hex >> token;
  return token;
}

std::optional<std::pair<API_Handler::IPC::MessageType, nlohmann::json>>
API_Handler::IPC::ParseMessage(const std::byte* data, size_t size,
                               uint64_t token) {
  if (size < sizeof(MessageHeader)) return std::nullopt;

  MessageHeader header;
  memcpy(&header, data, sizeof(MessageHeader));

  if (header.token != token) return std::nullopt;
  if (size < sizeof(MessageHeader) + header.payload_size) return std::nullopt;

  const std::byte* payload = data + sizeof(MessageHeader);

  if (header.message_type == MessageType::kToken &&
      header.payload_size == sizeof(uint32_t)) {
    uint32_t token_value;
    memcpy(&token_value, payload, sizeof(uint32_t));
    return std::make_pair(header.message_type,
                          nlohmann::json{{"token", token_value}});
  }

  if (header.message_type == MessageType::kImageStep &&
      header.payload_size == 3 * sizeof(uint32_t)) {
    std::array<uint32_t, 3> vals;
    memcpy(vals.data(), payload, sizeof(vals));
    return std::make_pair(
        header.message_type,
        nlohmann::json{
            {"phase", vals[0]}, {"step", vals[1]}, {"total_steps", vals[2]}});
  }

  nlohmann::json json_payload;
  if (header.payload_size > 0) {
    try {
      const auto* start = reinterpret_cast<const char*>(payload);
      json_payload = nlohmann::json::parse(start, start + header.payload_size);
    } catch (...) {
      return std::nullopt;
    }
  }

  return std::make_pair(header.message_type, json_payload);
}

// ---------------------------------------------------------------------------
// Server - lifecycle and command orchestration (platform-agnostic).
// ---------------------------------------------------------------------------

API_Handler::Server::Server(API_Handler::Logger& logger,
                            std::string& ihv_errors)
    : IPC(GenerateToken()), logger_(logger), ihv_errors_(ihv_errors) {}

API_Handler::Server::~Server() { Stop(); }

bool API_Handler::Server::Start() {
  if (running_) {
    return true;
  }

  if (!CreatePipes()) {
    logger_(LogLevel::kError,
            "Failed to create IPC channels for IHV subprocess");
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
    StopProcess();
    ClosePipes();
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
    WriteMessage(Channel::kCommand, MessageType::kShutdown, {});
  } catch (...) {
    // Best-effort graceful shutdown: if the shutdown message cannot be sent
    // (pipe already broken or the subprocess already gone), ignore it —
    // StopProcess() below terminates the subprocess unconditionally.
  }

  StopProcess();
  ClosePipes();
  logger_(LogLevel::kInfo, "IHV subprocess stopped");
}

nlohmann::json API_Handler::Server::SendMessage(MessageType command,
                                                const nlohmann::json& payload,
                                                int timeout_ms) {
  if (!IsRunning()) {
    return {{"success", false}, {"error", "Subprocess not running"}};
  }

  if (std::string payload_str = payload.dump(); !WriteMessage(
          Channel::kCommand, command, std::as_bytes(std::span(payload_str)))) {
    return {{"success", false}, {"error", "Failed to send command"}};
  }

  auto response = ReadMessage(Channel::kResponse, timeout_ms);
  if (!response) {
    return {{"success", false}, {"error", "No response from subprocess"}};
  }

  return response->second;
}

nlohmann::json API_Handler::Server::SendInferCommand(
    const nlohmann::json& payload, const TokenCallback& token_callback,
    const ImageStepCallback& image_step_callback, int timeout_ms) {
  if (!IsRunning()) {
    return {{"success", false}, {"error", "Subprocess not running"}};
  }

  if (std::string infer_payload_str = payload.dump();
      !WriteMessage(Channel::kCommand, MessageType::kInfer,
                    std::as_bytes(std::span(infer_payload_str)))) {
    return {{"success", false}, {"error", "Failed to send command"}};
  }

  while (true) {
    auto response = ReadMessage(Channel::kResponse, timeout_ms);
    if (!response) {
      if (!IsRunning()) {
        return {{"success", false}, {"error", "Subprocess crashed"}};
      }
      continue;
    }

    if (response->first == MessageType::kToken) {
      if (token_callback) {
        uint32_t token = response->second.value("token", 0u);
        token_callback(token);
      }
    } else if (response->first == MessageType::kImageStep) {
      if (image_step_callback) {
        uint32_t phase = response->second.value("phase", 0u);
        uint32_t step = response->second.value("step", 0u);
        uint32_t total_steps = response->second.value("total_steps", 0u);
        image_step_callback(phase, step, total_steps);
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

void API_Handler::Server::HandleLogBytes(const std::byte* data,
                                         size_t size) const {
  if (size < sizeof(MessageHeader)) {
    return;
  }

  MessageHeader header;
  memcpy(&header, data, sizeof(MessageHeader));

  if (header.token != token_ || header.message_type != MessageType::kLog ||
      header.payload_size == 0 ||
      size < sizeof(MessageHeader) + header.payload_size) {
    return;
  }

  try {
    // The payload is JSON text, not raw bytes: reinterpret the byte buffer as
    // a char range so nlohmann::json::parse can consume it (its iterator-pair
    // overload requires char-like elements, so std::byte cannot be used here).
    const auto* start =
        reinterpret_cast<const char*>(data + sizeof(MessageHeader));
    auto payload = nlohmann::json::parse(start, start + header.payload_size);
    int level = payload.value("level", 0);
    std::string message = payload.value("message", "");
    logger_(static_cast<API_Handler::LogLevel>(level), message);
  } catch (...) {
    // Best-effort logging: a malformed log frame is dropped rather than
    // disrupting the log-reader thread. Losing one diagnostic line is
    // preferable to tearing the thread down.
  }
}

// ---------------------------------------------------------------------------
// Client - command handling (platform-agnostic).
// ---------------------------------------------------------------------------

API_Handler::Client::Client(uint64_t token) : IPC(token) {}

API_Handler::Client::~Client() = default;

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

void API_Handler::Client::ProcessCommands() {
  while (true) {
    if (auto msg = ReadMessage(Channel::kCommand, kIPCTimeoutLong);
        !msg || !HandleMessage(msg->first, msg->second)) {
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
                                        const std::string& msg) {
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
        scenario_name_ = payload.value("scenario_name", "");
        model_base_name_ = payload.value("model_base_name", "");
        model_path_ = payload.value("model_path", "");
        deps_dir_ = payload.value("deps_dir", "");
        ep_settings_ = payload.value("ep_settings", nlohmann::json::object());

        auto ipc_logger = [this](API_Handler::LogLevel level,
                                 const std::string& msg) {
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

        if (!local_handler_->Setup(ep_name_, scenario_name_, model_base_name_,
                                   model_path_, deps_dir_, ep_settings_,
                                   device_type_)) {
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

        auto callback_type = static_cast<API_IHV_Callback_Type>(
            payload.value("callback_type", 0));
        bool is_image = (callback_type == API_IHV_CB_ImageStep);

        if (auto adapter = CallbackAdapter::Create(callback_type); adapter) {
          // Image scenarios pass a prompt string; others pass a token array.
          std::string prompt;
          std::vector<uint32_t> input_tokens;
          const void* input_ptr = nullptr;
          unsigned input_size = 0;
          if (is_image) {
            prompt = payload.value("prompt", "");
            input_ptr = prompt.c_str();
            input_size = static_cast<unsigned>(prompt.size());
          } else {
            input_tokens = payload.value("input", std::vector<uint32_t>{});
            input_ptr = input_tokens.data();
            input_size = static_cast<unsigned>(input_tokens.size());
          }

          API_IHV_IO_Data_t io_data = {input_ptr, input_size, nullptr, 0,
                                       adapter->GetCallback()};

          adapter->Start();
          bool adapter_result = local_handler_->Infer(io_data);
          adapter->Finish();

          nlohmann::json adapter_response;
          adapter_response["adapter_data"] = adapter->Serialize();
          if (is_image && io_data.output && io_data.output_size > 0) {
            const auto* output_ptr =
                static_cast<const uint8_t*>(io_data.output);
            adapter_response["output_bytes"] = std::vector<uint8_t>(
                output_ptr, output_ptr + io_data.output_size);
          }
          if (!adapter_result) {
            adapter_response["ihv_errors"] = local_handler_->GetIHVErrors();
          }
          SendResponse(adapter_result, adapter_response);
          return true;
        }

        bool result;
        nlohmann::json response_data;

        if (is_image) {
          std::string prompt = payload.value("prompt", "");
          API_IHV_IO_Data_t io_data = {
              prompt.c_str(),
              static_cast<unsigned>(prompt.size()),
              nullptr,
              0,
              {API_IHV_CB_ImageStep, this,
               reinterpret_cast<void*>(ImageStepCallbackStatic)}};

          result = local_handler_->Infer(io_data);

          if (io_data.output && io_data.output_size > 0) {
            const auto* output_ptr =
                static_cast<const uint8_t*>(io_data.output);
            response_data["output_bytes"] = std::vector<uint8_t>(
                output_ptr, output_ptr + io_data.output_size);
          }
        } else {
          auto input_tokens = payload.value("input", std::vector<uint32_t>{});
          API_IHV_IO_Data_t io_data = {
              input_tokens.data(),
              static_cast<unsigned>(input_tokens.size()),
              nullptr,
              0,
              {API_IHV_CB_Token, this, reinterpret_cast<void*>(TokenCallback)}};

          result = local_handler_->Infer(io_data);

          if (io_data.output && io_data.output_size > 0) {
            const auto* output_ptr =
                static_cast<const uint32_t*>(io_data.output);
            response_data["output"] = std::vector<uint32_t>(
                output_ptr, output_ptr + io_data.output_size);
          }
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
                                       const nlohmann::json& data) const {
  nlohmann::json response = data;
  response["success"] = success;
  std::string response_str = response.dump();
  WriteMessage(Channel::kResponse, MessageType::kResult,
               std::as_bytes(std::span(response_str)));
}

void API_Handler::Client::SendLog(API_IHV_LogLevel level,
                                  const std::string& message) const {
  nlohmann::json payload = {{"level", static_cast<int>(level)},
                            {"message", message}};
  std::string payload_str = payload.dump();
  WriteMessage(Channel::kLog, MessageType::kLog,
               std::as_bytes(std::span(payload_str)));
}

void API_Handler::Client::SendToken(uint32_t token_value) const {
  WriteMessage(Channel::kResponse, MessageType::kToken,
               std::as_bytes(std::span(&token_value, 1)));
}

void API_Handler::Client::SendImageStep(uint32_t phase, uint32_t step,
                                        uint32_t total_steps) const {
  std::array<uint32_t, 3> vals = {phase, step, total_steps};
  WriteMessage(Channel::kResponse, MessageType::kImageStep,
               std::as_bytes(std::span(vals)));
}

}  // namespace cil

#endif  // IHV_SUBPROCESS
