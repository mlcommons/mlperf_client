#include "llama2_inference.h"

#include <filesystem>

#include "../mlp_ort_genai_config.h"
#include "math_utils.h"

// NVTX Ranges for profiling and analysis
// Enabled via Cmake
#ifdef USE_NVTX_RANGES
#include "nvtx3/nvToolsExt.h"
#endif

class ScopedNvtx {
 public:
  ScopedNvtx() = delete;
  ScopedNvtx(const char* range_title) {
#ifdef USE_NVTX_RANGES
    nvtxRangePushA(range_title);
#endif  // USE_NVTX_RANGES
  };
  ScopedNvtx(ScopedNvtx&) = delete;
  ScopedNvtx(ScopedNvtx&&) = delete;

  ~ScopedNvtx() {
#ifdef USE_NVTX_RANGES
    nvtxRangePop();
#endif  // USE_NVTX_RANGES
  };
};

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

namespace cil {
namespace IHV {
namespace infer {

Llama2Inference::Llama2Inference(
    const std::string& model_path, const std::string& ep_name,
    const std::string& deps_dir,
    const OrtGenAIExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInference(model_path, ep_name, deps_dir, ep_settings, logger),
      config_(logger) {
  const auto model_parent_path =
      std::filesystem::path(model_path_).parent_path();
  auto model_folder = fs::current_path().append(model_parent_path.string());

  // Handle genai_config.json and environment variables

  if (ep_name.find("OrtGenAI") != std::string::npos) {
    SetupGenaiConfigForDirectML();
  }

  if (!error_message_.empty()) return;

  try {
    if (!oga_model_)
      CheckResult(OgaCreateModel(model_folder.string().c_str(), &oga_model_));
  } catch (const std::exception& e) {
    error_message_ = e.what();
    return;
  }
}

void Llama2Inference::Init(const nlohmann::json& model_config) {
  BaseInference::Init();
  if (!error_message_.empty()) return;

  if (initialized_) {
    error_message_ =
        "OrtGenAI->Init called but it already has been initialized!";
    return;
  }

  bool loaded = config_.LoadFromJson(model_config);
  if (!loaded) {
    error_message_ = "Failed to load model configuration";
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
      CheckResult(
        OgaGeneratorParamsSetSearchNumber(oga_gen_params_, "max_length", 
                                          config_.search.max_total_length));

    }
  } catch (const std::exception& e) {
    error_message_ = e.what();
    return;
  }
  output_data_.reserve(config_.search.max_length);
  initialized_ = true;
}

const std::vector<int32_t>& Llama2Inference::Run(
    const int32_t* const token_ptr, uint32_t token_count,
    std::function<void(uint32_t)> token_callback) {
  if (!initialized_) {
    error_message_ = "Llama2Inference::Run : Object not initialized!";
    return output_data_;
  }
  if (!token_ptr) {
    error_message_ = "Llama2Inference::Run : Invalid token pointer was passed.";
    return output_data_;
  }
  if (token_count == 0) {
    error_message_ = "Llama2Inference::Run : Token count is zero!";
    return output_data_;
  }

  output_data_.clear();
  try {
    // Custom ORT GenAI function, original API
    // does not support adding your own tokens

    CheckResult(OgaCreateSequences(&oga_benchmark_sequences_));
    CheckResult(OgaAppendTokenSequence(token_ptr, token_count,
                                       oga_benchmark_sequences_));

    CheckResult(OgaGeneratorParamsSetInputSequences(oga_gen_params_,
                                                    oga_benchmark_sequences_));

    // This should be in Init, but Init it does not have info about the input
    CheckResult(
        OgaCreateGenerator(oga_model_, oga_gen_params_, &oga_bench_generator_));

    // Run Benchmark
    {
      ScopedNvtx bench("Benchmark");
      for (size_t i = 0; i < config_.search.max_length; i++) {
        {
          ScopedNvtx logit_range("Comp. Logit");
          CheckResult(OgaGenerator_ComputeLogits(oga_bench_generator_));
        }
        {
          ScopedNvtx logit_range("Gene. Token");
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
    // Clean up locals
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
    return output_data_;
  }

  return output_data_;
}

void Llama2Inference::Deinit() {
  BaseInference::Deinit();
  if (!initialized_) {
    error_message_ = "OrtGenAI->DeInit called before it has been initialized!";
    return;
  }
  if (oga_gen_params_) {
    OgaDestroyGeneratorParams(oga_gen_params_);
    oga_gen_params_ = 0;
  }
  if (oga_bench_generator_) {
    OgaDestroyGenerator(oga_bench_generator_);
    oga_bench_generator_ = 0;
  }
  if (oga_benchmark_sequences_) {
    OgaDestroySequences(oga_benchmark_sequences_);
    oga_benchmark_sequences_ = 0;
  }
  output_data_.clear();
  initialized_ = false;
}

void cil::IHV::infer::Llama2Inference::SetupGenaiConfigForDirectML() {
  // Get the directory of model_file_path

  const auto& luuid_h = ep_settings_.GetLuidH();
  const auto& luuid_l = ep_settings_.GetLuidL();

  if (!luuid_h || !luuid_l) return;

  fs::path model_dir = fs::path(model_path_).parent_path();
  // Construct path to genai_config.json
  fs::path genai_config_path = model_dir / "genai_config.json";
  if (!fs::exists(genai_config_path)) {
    logger_(LogLevel::kError,
            "genai_config.json does not exist in " + model_dir.string());
    error_message_ =
        "genai_config.json does not exist in " + model_dir.string();
    return;
  }

  nlohmann::json genai_config;
  std::ifstream genai_config_reader{genai_config_path};
  try {
    genai_config_reader >> genai_config;
    genai_config_reader.close();

    std::vector<std::reference_wrapper<nlohmann::json>> dml_provider_options{};
    for (auto& value : genai_config["model"]) {
      if (value.is_object() &&
          value.contains("session_options")) {  // Check if value is a
                                                // JSON object
        auto& session_options = value["session_options"];
        if (session_options.contains("provider_options")) {
          for (auto& option : session_options["provider_options"]) {
            if (option.contains("dml")) {
              dml_provider_options.push_back(std::ref(option["dml"]));
            }
          }
        }
      }
    }

    if (dml_provider_options.empty()) return;

    backup_genai_config = {genai_config_path.string(), genai_config};

    for (auto& dml_provider_option : dml_provider_options) {
      dml_provider_option.get()["luid_high_part"] = std::to_string(luuid_h.value());
      dml_provider_option.get()["luid_low_part"] = std::to_string(luuid_l.value());
    }
    std::ofstream genai_config_out{genai_config_path};
    genai_config_out << genai_config.dump(4);
    genai_config_out.close();

    logger_(LogLevel::kInfo, "Updated " + genai_config_path.string());

  } catch (const std::exception& e) {
    std::string error = e.what();
    logger_(LogLevel::kFatal, "Failed to parse genai_config.json: " + error);
    error_message_ = "Failed to parse genai_config.json";
  }
}

Llama2Inference::~Llama2Inference() {
  Deinit();  // This might not be required

  if (oga_model_) {
    OgaDestroyModel(oga_model_);
    oga_model_ = 0;
  }

  OgaShutdown();

  try {
    if (backup_genai_config.has_value()) {
      std::ofstream genai_config_out{backup_genai_config->first};
      genai_config_out << backup_genai_config->second.dump(4);
      genai_config_out.close();
    }
  } catch (const std::exception& e) {
    logger_(LogLevel::kFatal,
            "Failed to restore genai_config.json: " + std::string(e.what()));
  }

};

}  // namespace infer
}  // namespace IHV
}  // namespace cil
