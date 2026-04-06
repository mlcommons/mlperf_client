#include "llm_inference.h"

#include <log4cxx/logger.h>

#include <sstream>

#include "../api_handler.h"

using namespace log4cxx;

namespace cil::infer {

LLMInference::LLMInference(const std::string& model_type,
                           const std::string& model_path,
                           const std::string& deps_dir, EP ep,
                           const nlohmann::json& ep_settings,
                           const std::string& library_path)
    : BaseInference(model_path, deps_dir, ep, ep_settings,
                    utils::StringToLowerCase(model_type),
                    utils::StringReplaceChar(model_type, '.', '_') + "Executor",
                    library_path) {}

LLMInference::~LLMInference() {
#ifdef HW_MONITORING
  if (hw_monitor_thread_.joinable()) {
    // Stop hardware monitoring thread
    hw_monitor_collect_.store(false, std::memory_order_acquire);
    hw_monitor_stop_.store(true, std::memory_order_acquire);
    hw_monitor_thread_.join();
  }
#endif
}

bool LLMInference::IsValid() const { return api_handler_->IsLoaded(); }

void LLMInference::ClearErrorMessage() {
  if (api_handler_) {
    api_handler_->ClearErrorMessage();
  }
  SetErrorMessage("");
}

void LLMInference::Init(const std::string& config) {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }

  auto init_start_time = std::chrono::high_resolution_clock::now();

  if (!api_handler_->Init(config)) {
    SetErrorMessage("Failed to initialize IHV");
    return;
  }

  auto init_end_time = std::chrono::high_resolution_clock::now();

  LogTime("IHV Init (Session creation) time: ", init_start_time, init_end_time);

  SetErrorMessage("");

#ifdef HW_MONITORING
  hw_monitor_stop_.store(false, std::memory_order_acquire);
  hw_monitor_collect_.store(false, std::memory_order_acquire);
  hw_monitor_thread_ = std::thread([this]() {
    while (true) {
      // Stop collecting hardware info
      if (hw_monitor_stop_.load(std::memory_order_acquire)) {
        break;
      }

      if (hw_monitor_collect_.load(std::memory_order_acquire)) {
        // Record hardware info sample
        LogHardwareInfo();
      }

      // Wait for the next interval
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });
#endif
}

void LLMInference::TokenCallback(void* object, Token token) {
  auto* obj = static_cast<LLMInference*>(object);
  obj->tokens_.emplace_back(token);
  obj->timestamps_.emplace_back(Clock::now());
}

void LLMInference::LogHardwareInfo() {
  std::scoped_lock lock(hw_info_mutex_);

  // Log hardware info
  auto system_info = cil::SystemInfoProvider::Instance();
  hw_info_records_.emplace_back(system_info->GetMemoryInfo(true), Clock::now());
}

LLMInference::Result LLMInference::Run(const std::vector<Token>& input_data) {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return {};
  }

  tokens_.clear();
  timestamps_.clear();
  hw_info_records_.clear();

  // Create callback info structure
  API_IHV_Callback_t cb{API_IHV_Callback_Type::API_IHV_CB_Token, this,
                        reinterpret_cast<void*>(&TokenCallback)};

  API_IHV_IO_Data_t io_data = {input_data.data(),
                               static_cast<unsigned>(input_data.size()),
                               nullptr, 0, cb};

  // Store hardware info before inference
  // Will not affect timing as it's done before start_time and any
  // inference call callbacks
  LogHardwareInfo();

#ifdef HW_MONITORING
  // Start hardware monitoring
  hw_monitor_collect_.store(true, std::memory_order_acquire);
#endif

  // MLPerf Power - "power_begin", "value": "02-25-2025 17:38:15.269"
  LOG4CXX_INFO(GetLogger(), "power_begin");

  const auto start_time = Clock::now();
  if (!api_handler_->Infer(io_data)) {
    SetErrorMessage("Failed to run inference for IHV");
    return {};
  }
  const auto end_time = Clock::now();

  // MLPerf Power - "power_end", "value": "02-25-2025 17:39:15.269"
  LOG4CXX_INFO(GetLogger(), "power_end");

#ifdef HW_MONITORING
  // Stop hardware monitoring
  hw_monitor_collect_.store(false, std::memory_order_acquire);
#endif

  LogHardwareInfo();

  LogTime("Ran inference and got " + std::to_string(tokens_.size()) +
              " tokens and " + std::to_string(timestamps_.size()) +
              " timestamps in ",
          start_time, end_time);

  if (timestamps_.size() > 1) {
    std::stringstream ss;
    ss << "TTFT " << std::fixed << std::setprecision(6)
       << std::chrono::duration_cast<MillisecDuration>(timestamps_[0] -
                                                       start_time)
              .count()
       << "ms, 2nd+ token latency "
       << std::chrono::duration_cast<MillisecDuration>(end_time -
                                                       timestamps_[0])
                  .count() /
              (timestamps_.size() - 1)
       << "ms";

    LOG4CXX_INFO(GetLogger(), ss.str());
  } else {
    LOG4CXX_INFO(GetLogger(), "Not enough timestamp samples");
  }

  SetErrorMessage("");

  // Actual tokens were returned through IO data
  if (io_data.output_size != 0 && io_data.output != nullptr) {
    if (io_data.output_size != tokens_.size()) {
      LOG4CXX_WARN(GetLogger(),
                   "Inconsistent number of tokens in IO structure. Got " +
                       std::to_string(io_data.output_size) + " tokens.");
    }
    LOG4CXX_WARN(GetLogger(), "Replacing callback tokens with IO data.");
    const auto* tokens_ptr = reinterpret_cast<const Token*>(io_data.output);
    tokens_.assign(tokens_ptr, tokens_ptr + io_data.output_size);
  }

  return {input_data.size(), tokens_,    timestamps_,
          hw_info_records_,  start_time, end_time};
}

void LLMInference::Deinit() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }

  if (!api_handler_->Deinit()) {
    SetErrorMessage("Failed to deinitialize IHV");
    return;
  }

  SetErrorMessage("");

#ifdef HW_MONITORING
  // Stop hardware monitoring thread
  hw_monitor_collect_.store(false, std::memory_order_acquire);
  hw_monitor_stop_.store(true, std::memory_order_acquire);
  hw_monitor_thread_.join();
#endif
}

void LLMInference::Prepare() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }

  if (!api_handler_->Prepare()) {
    SetErrorMessage("Failed to prepare IHV");
    return;
  }

  SetErrorMessage("");
}

void LLMInference::Reset() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage("IHV not loaded!: " + library_path_);
    return;
  }

  if (!api_handler_->Reset()) {
    SetErrorMessage("Failed to reset IHV");
    return;
  }

  SetErrorMessage("");
}

}  // namespace cil::infer