#include "llm_inference.h"

#include <format>
#include <filesystem>
#include <unordered_set>

#include "math_utils.h"

static __forceinline void CheckResult(OgaResult* result) {
  if (result) {
    std::string err_str = OgaResultGetError(result);
    OgaDestroyResult(result);
    throw std::runtime_error(err_str);
  }
}

constexpr bool ENABLE_EXTERNAL_DEVICE_ENABLED = false;

using namespace cil::infer;
namespace fs = std::filesystem;

namespace {
  const std::unordered_set<std::string> kEPConfigSetup = {"DirectML", "OpenVINO"};
}

namespace cil {
namespace IHV {
namespace infer {

LLMInference::LLMInference(
    const std::string& model_path, const std::string& model_name,
    const std::string& ep_name, const std::string& deps_dir,
    const WindowsMLExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInference(model_path, model_name, ep_name, deps_dir, ep_settings,
                    logger),
      config_(logger) {}

void LLMInference::Init(const nlohmann::json& model_config,
                        std::optional<API_IHV_DeviceID_t>) {
  BaseInference::Init();
  if (!error_message_.empty()) return;

  if (initialized_) {
    error_message_ =
        "WindowsML->Init called but it already has been initialized!";
    return;
  }

  if (!config_.LoadFromJson(model_config)) {
    error_message_ = "Failed to load model configuration";
    return;
  }

  AssignModelForDevices();

  const auto& device_id = ep_settings_.GetDeviceId().value_or(0);

  // Get the winml device at device_id, check bounds
  if (!winml_devices_.has_value() || device_id >= winml_devices_->size()) {
    logger_(LogLevel::kError,
            "Invalid device id: " + std::to_string(device_id));
    error_message_ = "Invalid device id: " + std::to_string(device_id);
    return;
  }

  const auto& device = (*winml_devices_)[device_id];

  if (device.model_path.empty()) {
    error_message_ = "No model found for device: " + std::to_string(device_id) +
                     " (EP: " + device.ep + ", Name: " + device.name + ")";
    return;
  }

  auto model_folder = fs::current_path().append(device.model_path);

  SetupGenaiConfigForEP(device.model_path);

  if (!error_message_.empty()) return;

  try {
    if (!oga_model_)
      CheckResult(OgaCreateModel(model_folder.string().c_str(), &oga_model_));

    if (oga_model_) {
      const char* device_type_active = nullptr;
      CheckResult(OgaModelGetDeviceType(oga_model_, &device_type_active));
      std::string device_type_str(device_type_active);
      OgaDestroyString(device_type_active);
      logger_(LogLevel::kInfo, "Using model: " + device.model_path +
                                   " on device: " + std::to_string(device_id) +
                                   " (" + device_type_str + ")");
    }
  } catch (const std::exception& e) {
    error_message_ = e.what();
    return;
  }

  // This generated a weird error when calling it for the second time,
  // didnt find anything in the docs of ORT GenAI. Disabled for now.
  auto ep_device_id = ep_settings_.GetDeviceId();
  if (ep_device_id.has_value() && ENABLE_EXTERNAL_DEVICE_ENABLED &&
      device_id_active_ != ep_device_id.value()) {
    device_id_active_ = ep_device_id.value();
    OgaSetCurrentGpuDeviceId(device_id_active_);
  }

  try {
    if (!oga_gen_params_) {
      CheckResult(OgaCreateGeneratorParams(oga_model_, &oga_gen_params_));
      CheckResult(OgaGeneratorParamsSetSearchNumber(oga_gen_params_, "top_p",
                                                    config_.search.top_p));
      CheckResult(OgaGeneratorParamsSetSearchNumber(oga_gen_params_, "top_k",
                                                    config_.search.top_k));
      CheckResult(OgaGeneratorParamsSetSearchNumber(
          oga_gen_params_, "temperature", config_.search.temperature));
      CheckResult(
          OgaGeneratorParamsSetSearchNumber(oga_gen_params_, "min_length", 1));
      CheckResult(OgaGeneratorParamsSetSearchNumber(
          oga_gen_params_, "max_length", config_.search.max_total_length));
    }
  } catch (const std::exception& e) {
    error_message_ = e.what();
    return;
  }
  output_data_.reserve(config_.search.max_length);
  initialized_ = true;
}

const std::vector<int32_t>& LLMInference::Run(
    const int32_t* const token_ptr, uint32_t token_count,
    std::function<void(uint32_t)> token_callback) {
  if (!initialized_) {
    error_message_ = "LLMInference::Run : Object not initialized!";
    return output_data_;
  }
  if (!token_ptr) {
    error_message_ = "LLMInference::Run : Invalid token pointer was passed.";
    return output_data_;
  }
  if (token_count == 0) {
    error_message_ = "LLMInference::Run : Token count is zero!";
    return output_data_;
  }

  output_data_.clear();
  try {
    // Custom ORT GenAI function, original API
    // does not support adding your own tokens

    CheckResult(OgaCreateSequences(&oga_benchmark_sequences_));
    CheckResult(OgaAppendTokenSequence(token_ptr, token_count,
                                       oga_benchmark_sequences_));

    // This should be in Init, but Init it does not have info about the input
    CheckResult(
        OgaCreateGenerator(oga_model_, oga_gen_params_, &oga_bench_generator_));
    CheckResult(OgaGenerator_AppendTokenSequences(oga_bench_generator_,
                                                  oga_benchmark_sequences_));

    // Run Benchmark
    {
      for (size_t i = 0; i < config_.search.max_length; i++) {
        {
          CheckResult(OgaGenerator_GenerateNextToken(oga_bench_generator_));
        }
        const int32_t num_tokens =
            OgaGenerator_GetSequenceCount(oga_bench_generator_, 0);
        int32_t new_token = OgaGenerator_GetSequenceData(oga_bench_generator_,
                                                         0)[num_tokens - 1];
        token_callback(new_token);
        output_data_.emplace_back(new_token);

        // early stopping
        if (OgaGenerator_IsDone(oga_bench_generator_) &&
            config_.search.stop_on_eos) {
          break;
        }
      }
    }

    if (oga_bench_generator_) {
      OgaDestroyGenerator(oga_bench_generator_);
      oga_bench_generator_ = 0;
    }

    if (oga_benchmark_sequences_) {
      OgaDestroySequences(oga_benchmark_sequences_);
      oga_benchmark_sequences_ = 0;
    }
  } catch (const std::exception& e) {
    error_message_ = e.what();
    output_data_.clear();
  }

  return output_data_;
}

void LLMInference::Deinit() {
  BaseInference::Deinit();
  if (!initialized_) {
    error_message_ = "WindowsML->DeInit called before it has been initialized!";
    return;
  }
  output_data_.clear();
  error_message_.clear();

  if (oga_bench_generator_) {
    OgaDestroyGenerator(oga_bench_generator_);
    oga_bench_generator_ = 0;
  }
  if (oga_gen_params_) {
    OgaDestroyGeneratorParams(oga_gen_params_);
    oga_gen_params_ = 0;
  }
  if (oga_benchmark_sequences_) {
    OgaDestroySequences(oga_benchmark_sequences_);
    oga_benchmark_sequences_ = 0;
  }
  if (oga_model_) {
    try {
      OgaDestroyModel(oga_model_);
    } catch (const std::exception& e) {
      logger_(LogLevel::kError,
              "Failed to destroy model: " + std::string(e.what()));
      error_message_ = "Failed to destroy model: " + std::string(e.what());
    }
    oga_model_ = 0;
  }
  if (getenv("DISABLE_OPENVINO_GENAI_NPU_L0")) {
    putenv("DISABLE_OPENVINO_GENAI_NPU_L0=");
  }

  try {
    if (backup_genai_config_.has_value()) {
      std::ofstream genai_config_out{backup_genai_config_->first};
      genai_config_out << backup_genai_config_->second.dump(4);
      genai_config_out.close();
    }
  } catch (const std::exception& e) {
    logger_(LogLevel::kFatal,
            "Failed to restore genai_config.json: " + std::string(e.what()));
  }

  initialized_ = false;
}

void LLMInference::SetupGenaiConfigForEP(const std::string& model_path) {
  const auto& device_id = ep_settings_.GetDeviceId().value_or(0);

  if (!winml_devices_.has_value() || winml_devices_->empty()) {
    error_message_ = "No WinML devices found";
    logger_(LogLevel::kError, error_message_);
    return;
  }

  if (winml_devices_->size() < device_id) {
    error_message_ = "Invalid device id";
    logger_(LogLevel::kError, error_message_);
    return;
  }

  const auto& winml_device = (*winml_devices_)[device_id];

  if (!kEPConfigSetup.contains(winml_device.ep)) {
    // No need to update config for EP
    return;
  }

  bool any_updated = false;

  // Update general DML options
  const auto update_dml_config = [&](auto& options) {
    if (!directml_devices_.has_value() || directml_devices_->empty()) {
      throw std::runtime_error("No DirectML devices found");
    }

    if (!winml_device.directml_device_entry.has_value() ||
        winml_device.directml_device_entry.value() >=
            directml_devices_->size()) {
      throw std::runtime_error("Invalid DirectML device id!");
    }

    const auto& device =
        directml_devices_.value()[winml_device.directml_device_entry.value()];
    const auto luuid_h = device.luid_high_part;
    const auto luuid_l = device.luid_low_part;

    options["luid"] = std::format("{}:{}", luuid_h, luuid_l);

    any_updated = true;
  };

  // Update OpenVINO specific options
  const auto update_ov_config = [&](auto& options, auto& genai_config) {
    constexpr size_t kMaxNpuAllocSize = 4032;

    genai_config["search"]["max_length"] = config_.search.max_total_length;

    const int max_prompt_len = 
        static_cast<int>(config_.search.max_total_length - config_.search.max_length);
    options["load_config"] = std::format(
        "{{\"NPU\":{{\"MAX_PROMPT_LEN\":\"{}\",\"MIN_RESPONSE_LEN\":\"{}\"{}}}}}",
        max_prompt_len,
        config_.search.max_length,
        (config_.search.max_total_length <= kMaxNpuAllocSize) || (model_name_ != "llama2") ?
          ",\"GENERATE_HINT\":\"BEST_PERF\"" : "");
    options["device_type"] = ep_settings_.GetDeviceType();

    if (config_.search.max_total_length > kMaxNpuAllocSize) {
      putenv("DISABLE_OPENVINO_GENAI_NPU_L0=1");
    }

    any_updated = true;
  };

  // Model path is already set in the constructor
  const fs::path model_dir(model_path);
  const fs::path genai_config_path = model_dir / "genai_config.json";
  if (!fs::exists(genai_config_path)) {
    error_message_ =
        "genai_config.json does not exist in " + model_dir.string();
    logger_(LogLevel::kError,
            error_message_);
    return;
  }

  try {
    std::ifstream genai_config_reader{genai_config_path};
    nlohmann::json genai_config;
    genai_config_reader >> genai_config;
    genai_config_reader.close();

    for (auto& value : genai_config["model"]) {
      if (value.is_object() &&
          value.contains("session_options")) {  // Check if value is a
                                                // JSON object
        auto& session_options = value["session_options"];
        if (session_options.contains("provider_options")) {
          for (auto& option : session_options["provider_options"]) {
            // Update provider specific options
            if (option.contains("dml")) {
              update_dml_config(option["dml"]);
            } else if (option.contains("OpenVINO")) {
              update_ov_config(option["OpenVINO"], genai_config);
            }
          }
        }
      }
    }

    if (any_updated) {
      backup_genai_config_ = {genai_config_path.string(), genai_config};
    } else {
      return;
    }

    std::ofstream genai_config_out{genai_config_path};
    genai_config_out << genai_config.dump(4);
    genai_config_out.close();

    logger_(LogLevel::kInfo, "Updated " + genai_config_path.string());
  } catch (const std::exception& e) {
    error_message_ = "Failed to generate genai_config.json";
    logger_(LogLevel::kFatal, std::format("{}: {}",
        error_message_, e.what()));
  }
}

LLMInference::~LLMInference() {
  if (initialized_) Deinit();

  OgaShutdown();
}

}  // namespace infer
}  // namespace IHV
}  // namespace cil
