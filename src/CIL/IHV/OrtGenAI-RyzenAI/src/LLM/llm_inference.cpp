#include "llm_inference.h"

#include <filesystem>

#include "../mlp_ort_genai_ryzenai_config.h"
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

namespace cil {
namespace IHV {
namespace infer {

LLMInference::LLMInference(const std::string& model_path,
                           const std::string& model_name,
                           const std::string& ep_name,
                           const std::string& deps_dir,
                           const OrtGenAIExecutionProviderSettings& ep_settings,
                           cil::Logger logger)
    : BaseInference(model_path, model_name, ep_name, deps_dir, ep_settings,
                    logger),
      config_(logger) {}

void LLMInference::Init(const nlohmann::json& model_config,
                        std::optional<API_IHV_DeviceID_t> device_id) {
  BaseInference::Init();
  if (!error_message_.empty()) return;

  if (initialized_) {
    error_message_ =
        "OrtGenAI->Init called but it already has been initialized!";
    return;
  }

  const auto model_parent_path =
      std::filesystem::path(model_path_).parent_path();
  auto model_folder = fs::current_path().append(model_parent_path.string());

  // Handle genai_config.json and environment variables

  if (ep_name_.find("RyzenAI") != std::string::npos) {
    SetupGenaiConfigForRyzenAI();
  }

  if (!error_message_.empty()) return;

  try {
    if (!oga_model_)
      CheckResult(OgaCreateModel(model_folder.string().c_str(), &oga_model_));
  } catch (const std::exception& e) {
    error_message_ = e.what();
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
      CheckResult(OgaGeneratorParamsSetSearchNumber(
          oga_gen_params_, "max_length", config_.search.max_total_length));
      CheckResult(OgaCreateGenerator(oga_model_, oga_gen_params_,
                                     &oga_bench_generator_));
      CheckResult(OgaCreateSequences(&oga_benchmark_sequences_));
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

    CheckResult(OgaAppendTokenSequence(token_ptr, token_count,
                                       oga_benchmark_sequences_));

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
    // Clean up locals
    if (oga_bench_generator_) {
      OgaDestroyGenerator(oga_bench_generator_);
      CheckResult(OgaCreateGenerator(oga_model_, oga_gen_params_,
                                     &oga_bench_generator_));
    }
    if (oga_benchmark_sequences_) {
      OgaDestroySequences(oga_benchmark_sequences_);
      CheckResult(OgaCreateSequences(&oga_benchmark_sequences_));
    }

  } catch (const std::exception& e) {
    error_message_ = e.what();
    output_data_.clear();
    return output_data_;
  }

  return output_data_;
}

void LLMInference::Deinit() {
  BaseInference::Deinit();
  if (!initialized_) {
    error_message_ = "OrtGenAI->DeInit called before it has been initialized!";
    return;
  }
  output_data_.clear();

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
  if (oga_model_) {
    OgaDestroyModel(oga_model_);
    oga_model_ = 0;
  }

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

  initialized_ = false;
  error_message_.clear();
}

void cil::IHV::infer::LLMInference::
    SetupGenaiConfigForRyzenAI() {  // Get the directory of model_file_path
  fs::path model_dir = fs::path(model_path_).parent_path();
  // Construct path to genai_config.json
  fs::path genai_config_path = model_dir / "genai_config.json";
  if (!fs::exists(genai_config_path)) return;

  try {
    // Load genai_config.json
    nlohmann::json genai_config;
    std::ifstream genai_config_in(genai_config_path);

    genai_config_in >> genai_config;
    genai_config_in.close();

    // Check if "model.decoder.session_options.custom_ops_library"
    // exists
    if (!genai_config.contains("model") ||
        !genai_config["model"].contains("decoder") ||
        !genai_config["model"]["decoder"].contains("session_options") ||
        !genai_config["model"]["decoder"]["session_options"].contains(
            "custom_ops_library"))
      return;

    backup_genai_config = {genai_config_path.string(), genai_config};

    std::string custom_ops_library_path =
        genai_config["model"]["decoder"]["session_options"]
                    ["custom_ops_library"];

    // Check if path is relative
    fs::path custom_ops_library_fs_path(custom_ops_library_path);
    if (custom_ops_library_fs_path.is_relative()) {
      // Convert to absolute path based on model_dir
      fs::path absolute_path =
          fs::absolute(fs::path(deps_dir_) / custom_ops_library_fs_path);

      // Update the path in genai_config
      genai_config["model"]["decoder"]["session_options"]
                  ["custom_ops_library"] = absolute_path.string();

      logger_(LogLevel::kInfo,
              "Updated custom_ops_library path in "
              "genai_config.json to absolute path: " +
                  absolute_path.string());
    }

    if (genai_config.contains("model") &&
        genai_config["model"].contains("decoder") &&
        genai_config["model"]["decoder"].contains("session_options") &&
        genai_config["model"]["decoder"]["session_options"].contains(
            "amd_options")) {
      if (genai_config["model"]["decoder"]["session_options"]["amd_options"]
              .contains("dd_cache")) {
        logger_(LogLevel::kInfo,
                "Found dd_cache in genai_config.json, updating to absolute "
                "path if necessary.");
        // Get the dd_cache path

        std::string dd_cache_path =
            genai_config["model"]["decoder"]["session_options"]["amd_options"]
                        ["dd_cache"];

        // Check if dd_cache_path is relative
        fs::path dd_cache_fs_path(dd_cache_path);
        if (dd_cache_fs_path.is_relative()) {
          // Convert to absolute path based on model_dir
          fs::path absolute_dd_cache_path =
              fs::absolute(model_dir / dd_cache_fs_path);
          // Update the path in genai_config
          genai_config["model"]["decoder"]["session_options"]["amd_options"]
                      ["dd_cache"] = absolute_dd_cache_path.string();
          logger_(
              LogLevel::kInfo,
              "Updated dd_cache path in genai_config.json to absolute path: " +
                  absolute_dd_cache_path.string());
        }
      }

      if (genai_config["model"]["decoder"]["session_options"]["amd_options"]
              .contains("dd_root")) {
        logger_(LogLevel::kInfo,
                "Found dd_root in genai_config.json, updating to absolute path "
                "if necessary.");

        std::string dd_root_path =
            genai_config["model"]["decoder"]["session_options"]["amd_options"]
                        ["dd_root"];

        // Check if dd_root_path is relative
        fs::path dd_root_fs_path(dd_root_path);
        if (dd_root_fs_path.is_relative()) {
          // Convert to absolute path based on model_dir
          fs::path absolute_dd_root_path =
              fs::absolute(fs::path(deps_dir_) / "");
          // Update the path in genai_config
          genai_config["model"]["decoder"]["session_options"]["amd_options"]
                      ["dd_root"] = absolute_dd_root_path.string();
          logger_(
              LogLevel::kInfo,
              "Updated dd_root path in genai_config.json to absolute path: " +
                  absolute_dd_root_path.string());
        }
      }
    }

    std::string config_file_path = "vaip_llm.json";
    fs::path config_file_fs_path(config_file_path);
    if (genai_config["model"]["decoder"]["session_options"]["provider_options"]
                .size() > 0 &&
        genai_config["model"]["decoder"]["session_options"]["provider_options"]
                    [0]
                        .contains("VitisAI"))
      genai_config["model"]["decoder"]["session_options"]["provider_options"][0]
                  ["VitisAI"]["config_file"] =
                      fs::absolute(fs::path(deps_dir_) / config_file_fs_path)
                          .string();

    // Save the updated genai_config.json
    std::ofstream genai_config_out(genai_config_path);
    genai_config_out << genai_config.dump(4);  // Pretty print with indentation
    genai_config_out.close();

  } catch (const std::exception& e) {
    std::string error = e.what();
    logger_(LogLevel::kFatal, "Failed to parse genai_config.json: " + error);
    error_message_ = "Failed to parse genai_config.json";
  }
}

LLMInference::~LLMInference() {
  // This might not be required

  if (initialized_) Deinit();

  OgaShutdown();
}
}  // namespace infer
}  // namespace IHV
}  // namespace cil
