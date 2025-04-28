#include "llama2_inference.h"

#include <log4cxx/logger.h>

#include <sstream>

#include "../api_handler.h"

using namespace log4cxx;

extern LoggerPtr llama2_executor_logger;

namespace cil::infer {

Llama2Inference::Llama2Inference(const std::string& model_path,
                                 const std::string& deps_dir, EP ep,
                                 const nlohmann::json& ep_settings,
                                 const std::string& library_path)
    : BaseInference(model_path, deps_dir, ep, ep_settings, "llama2",
                    "Llama2Executor", library_path) {}

Llama2Inference::~Llama2Inference() = default;

bool Llama2Inference::IsValid() const { return api_handler_->IsLoaded(); }

void Llama2Inference::Init(const std::string& config) {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage(api_handler_->LibraryName() +
                    " not loaded!: " + library_path_);
    return;
  }

  auto init_start_time = std::chrono::high_resolution_clock::now();

  if (!api_handler_->Init(config)) {
    SetErrorMessage("Failed to initialize " + api_type_);
    return;
  }

  auto init_end_time = std::chrono::high_resolution_clock::now();

  LogTime(api_type_ + " Init (Session creation) time: ", init_start_time,
          init_end_time);

  SetErrorMessage("");
}

void Llama2Inference::TokenCallback(void* object, Token token) {
  auto* obj = static_cast<Llama2Inference*>(object);
  obj->tokens_.emplace_back(token);
  obj->timestamps_.emplace_back(Clock::now());
}

Llama2Inference::Result Llama2Inference::Run(
    std::span<const Token> input_data) {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage(api_handler_->LibraryName() +
                    " not loaded!: " + library_path_);
    return {};
  }

  tokens_.clear();
  timestamps_.clear();

  // Create callback info structure
  API_IHV_Callback_t cb{API_IHV_Callback_Type::API_IHV_CB_Token, this,
                        reinterpret_cast<void*>(&TokenCallback)};

  API_IHV_IO_Data_t io_data = {input_data.data(),
                               static_cast<unsigned>(input_data.size()),
                               nullptr, 0, cb};

  const auto start_time = Clock::now();
  if (!api_handler_->Infer(io_data)) {
    SetErrorMessage("Failed to run inference for " + api_type_);
    return {};
  }
  const auto end_time = Clock::now();

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

  return {input_data.size(), tokens_, timestamps_, start_time, end_time};
}

void Llama2Inference::Deinit() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage(api_handler_->LibraryName() +
                    " not loaded!: " + library_path_);
    return;
  }

  if (!api_handler_->Deinit()) {
    SetErrorMessage("Failed to deinitialize " + api_type_);
    return;
  }

  SetErrorMessage("");
}

void Llama2Inference::Prepare() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage(api_handler_->LibraryName() +
                    " not loaded!: " + library_path_);
    return;
  }

  if (!api_handler_->Prepare()) {
    SetErrorMessage("Failed to prepare " + api_type_);
    return;
  }

  SetErrorMessage("");
}

void Llama2Inference::Reset() {
  if (!api_handler_->IsLoaded()) {
    SetErrorMessage(api_handler_->LibraryName() +
                    " not loaded!: " + library_path_);
    return;
  }

  if (!api_handler_->Reset()) {
    SetErrorMessage("Failed to reset " + api_type_);
    return;
  }

  SetErrorMessage("");
}

}  // namespace cil::infer