#include "base_inference.h"

#include <system_error>

#include "../api_handler.h"

using namespace log4cxx;

namespace cil {
namespace infer {

void BaseInference::LogTime(
    const std::string& desc,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& st,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& et) {
  auto duration_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(et - st).count();
  double duration_ms = duration_ns / 1e6;
  LOG4CXX_INFO(GetLogger(), desc << std::fixed << std::setprecision(6)
                                 << duration_ms << " ms\n");
}

BaseInference::BaseInference(const std::string& model_path,
                             const std::string& deps_dir, EP ep,
                             const nlohmann::json& ep_settings,
                             const std::string& scenario_name,
                             const std::string& model_base_name,
                             const std::string& logger_name,
                             const std::string& library_path)
    : model_path_(model_path),
      deps_dir_(deps_dir),
      ep_(ep),
      ep_settings_(ep_settings),
      benchmark_time_(0),
      scenario_name_(scenario_name),
      model_base_name_(model_base_name),
      library_path_(library_path) {
  std::error_code ec;
  saved_cwd_ = std::filesystem::current_path(ec);
  saved_cwd_valid_ = !ec;

  logger_ = Logger::getLogger(logger_name);

  auto logger = [this](API_Handler::LogLevel level, std::string message) {
    switch (level) {
      using enum API_Handler::LogLevel;
      case kInfo:
        LOG4CXX_INFO(logger_, message);
        break;
      case kWarning:
        LOG4CXX_WARN(logger_, message);
        break;
      case kError:
        LOG4CXX_ERROR(logger_, message);
        break;
      case kFatal:
        LOG4CXX_FATAL(logger_, message);
        break;
    }
  };

  api_handler_ = std::make_unique<API_Handler>(library_path, logger);

  if (!api_handler_->Setup(EPToString(ep), scenario_name_, model_base_name_,
                           model_path, deps_dir_, ep_settings, device_type_))
    error_message_ = "Failed to setup IHV";
}

BaseInference::~BaseInference() {
  try {
    try {
      if (api_handler_ && !api_handler_->Release()) {
        LOG4CXX_WARN(logger_, "Failed to release IHV");
      }
    } catch (const std::exception& e) {
      LOG4CXX_ERROR(logger_, "Exception while releasing IHV: " << e.what());
    } catch (...) {
      LOG4CXX_ERROR(logger_, "Unknown exception while releasing IHV");
    }
  } catch (...) {
    // log4cxx threw too; nothing safe left to do in a dtor
  }

  if (saved_cwd_valid_) {
    std::error_code ec;
    std::filesystem::current_path(saved_cwd_, ec);
  }
}

double cil::infer::BaseInference::GetBenchmarkTime() const {
  return benchmark_time_;
}

std::string BaseInference::GetDeviceType() const { return device_type_; }

std::string BaseInference::GetErrorMessage(bool include_ep_errors) const {
  if (include_ep_errors) {
    if (std::string ihv_errors = GetEPErrorMessages(); !ihv_errors.empty()) {
      return error_message_ + ":\n" + ihv_errors;
    }
  }
  return error_message_;
}

log4cxx::LoggerPtr BaseInference::GetLogger() const { return logger_; }

std::string BaseInference::GetEPErrorMessages() const {
  if (api_handler_) {
    return api_handler_->GetIHVErrors();
  }
  return "";
}

void BaseInference::SetBenchmarkTime(double benchmark_time) {
  benchmark_time_ = benchmark_time;
}

void BaseInference::SetDeviceType(const std::string& device_type) {
  device_type_ = device_type;
}

void BaseInference::SetErrorMessage(const std::string& error_message) {
  error_message_ = error_message;
}

void BaseInference::SetMetadata(const std::string& key, const std::any& value) {
  metadata_[key] = value;
}

std::any BaseInference::GetMetadata(const std::string& key) const {
  if (auto it = metadata_.find(key); it != metadata_.end()) {
    return it->second;
  }
  return std::any();
}

}  // namespace infer
}  // namespace cil