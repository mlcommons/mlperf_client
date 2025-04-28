#include "system_controller.h"

#include <log4cxx/logger.h>

#include <csignal>
#include <fstream>
#include <iostream>

#include "benchmark/runner.h"
#include "benchmark_logger.h"
#include "ep_dependencies_manager.h"
#include "execution_config.h"
#include "execution_provider.h"
#include "progress_tracker_handler.h"
#include "unpacker.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;
namespace utils = cil::utils;

const LoggerPtr loggerSystemController(Logger::getLogger("SystemController"));

namespace cli {

const std::vector<std::string> SystemController::kSupportedModels = {"llama2"};

std::atomic<bool> SystemController::interrupt_(false);

SystemController::SystemController(const std::string& json_config_path,
                                   std::shared_ptr<cil::Unpacker> unpacker,
                                   const std::string& output_dir,
                                   const std::string& data_dir)
    : json_config_path_(json_config_path),
      unpacker_(unpacker),
      output_dir_(output_dir),
      data_dir_(data_dir.empty() ? "data" : data_dir) {
  std::signal(SIGINT, &SystemController::InterruptHandler);
}

SystemController::~SystemController() = default;

bool SystemController::Config() {
  using enum cil::Unpacker::Asset;

  if (!cil::ExecutionConfig::isConfigFileValid(json_config_path_)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Config file is not a json file: " << json_config_path_);
    return false;
  }

  std::string config_schema_path;
  if (!unpacker_->UnpackAsset(kConfigSchema, config_schema_path)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack config schema, aborting...");
    return false;
  }

  // Config validation and parsing
  std::string config_verification_file_path;
  if (!unpacker_->UnpackAsset(kConfigVerificationFile,
                              config_verification_file_path)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack config verification file, aborting...");
    return false;
  }

  config_ = std::make_unique<cil::ExecutionConfig>();
  bool json_loaded = config_->ValidateAndParse(
      json_config_path_, config_schema_path, config_verification_file_path);

  if (!json_loaded) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Unable to load scenarios from config, aborting...");
    return false;
  }

  if (config_->IsConfigVerified()) {
    LOG4CXX_INFO(loggerSystemController, "Configuration tested by MLCommons");
  } else {
    LOG4CXX_WARN(loggerSystemController,
                 "Configuration NOT tested by MLCommons");
  }

  if (!config_->GetSystemConfig().IsTempPathCorrect()) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Temp path is not valid, aborting...");
    return false;
  }
  if (!config_->GetSystemConfig().IsBaseDirCorrect()) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Base dir is not a valid absolute path, aborting...");
    return false;
  }

  auto temp_path = config_->GetSystemConfig().GetTempPath();
  if (temp_path.empty()) {
    temp_path = unpacker_->GetDepsDir();
    LOG4CXX_WARN(
        loggerSystemController,
        "Temp path is not set in the config; using default system temp path: "
            << temp_path);
  } else {
    LOG4CXX_INFO(loggerSystemController, "Temp path: " << temp_path);
    // create the temp directory if it does not exist
    if (!fs::exists(temp_path) && !utils::CreateDirectory(temp_path)) {
      LOG4CXX_ERROR(loggerSystemController,
                    "Failed to create temp directory, aborting...");
      return false;
    }
    unpacker_->SetDepsDir(temp_path);
  }

  std::unordered_map<std::string, std::string> eps_dependencies_dest;
  bool has_llama2_scenario = false;
  for (const auto& model : config_->GetScenarios()) {
    const auto& model_name = model.GetName();
    if (utils::StringToLowerCase(model_name) == "llama2")
      has_llama2_scenario = true;

    if (std::find(kSupportedModels.begin(), kSupportedModels.end(),
                  model_name) == kSupportedModels.end()) {
      LOG4CXX_ERROR(loggerSystemController,
                    "Model "
                        << model_name
                        << " is not supported in this build, aborting...\nUse "
                           "-h or --help for more information.");
      return false;
    }

    for (const auto& ep : model.GetExecutionProviders()) {
      const auto& ep_name = ep.GetName();

      auto is_supported_ep =
          cil::utils::IsEpSupportedOnThisPlatform("", ep_name);

      if (!is_supported_ep) {
        LOG4CXX_INFO(loggerSystemController,
                     "Invalid execution provider \""
                         << ep_name
                         << "\" found in configuration, aborting...");
        return false;
      }

      auto is_ep_known_ihv = cil::IsEPFromKnownIHV(ep_name);

      auto empty_library_path = ep.GetLibraryPath().empty();

      // We use internal dependencies from JSON if it's a known IHV and
      // the library path was not provided
      auto internal_ep_dependencies = empty_library_path && is_ep_known_ihv;

      if (internal_ep_dependencies)
        eps_dependencies_dest.insert(
            {ep.GetName(), cil::GetExecutionProviderParentLocation(
                               ep, unpacker_->GetDepsDir())
                               .string()});
    }
  }

  if (!unpacker_->UnpackAsset(kOutputResultsSchema,
                              output_results_schema_path_)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack output results schema, aborting...");
    return false;
  }

  if (!unpacker_->UnpackAsset(kDataVerificationFileSchema,
                              data_verification_file_schema_path_)) {
    LOG4CXX_ERROR(
        loggerSystemController,
        "Failed to unpack data verification file schema, aborting...");
    return false;
  }

  if (has_llama2_scenario && !unpacker_->UnpackAsset(kLlama2InputFileSchema,
                                                     input_file_schema_path_)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack llama2 input file schema, aborting...");
    return false;
  }

  auto ep_dependencies_config_path =
      config_->GetSystemConfig().GetEPDependenciesConfigPath();
  if (ep_dependencies_config_path.empty() &&
      !unpacker_->UnpackAsset(kEPDependenciesConfig,
                              ep_dependencies_config_path)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack EP dependencies config, aborting... "
                      << ep_dependencies_config_path);
    return false;
  }

  std::string ep_dependencies_config_schema_path;
  if (!unpacker_->UnpackAsset(kEPDependenciesConfigSchema,
                              ep_dependencies_config_schema_path)) {
    LOG4CXX_ERROR(
        loggerSystemController,
        "Failed to unpack EP dependencies config schema, aborting...");
    return false;
  }

  ep_dependencies_manager_ = std::make_unique<cil::EPDependenciesManager>(
      eps_dependencies_dest, ep_dependencies_config_path,
      ep_dependencies_config_schema_path, unpacker_->GetDepsDir());

  if (!ep_dependencies_manager_->Initialize()) return false;

  std::string deps_dir = unpacker_->GetDepsDir();
  size_t disc_space = utils::GetAvailableDiskSpace(deps_dir);
  if (disc_space == -1) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Can not determine available disc space, aborting...");
    return false;
  }
  return true;
}

void SystemController::DisplayResultsStage(
    cil::BenchmarkLogger& results_logger) const {
  results_logger.DisplayAllResults();
  results_logger.Clear();
}

const std::vector<cil::ScenarioConfig>& SystemController::GetScenarios() const {
  return config_->GetScenarios();
}

const std::string& SystemController::GetSystemConfigTempPath() const {
  return config_->GetSystemConfig().GetTempPath();
}

const std::string& SystemController::GetSystemDownloadBehavior() const {
  return config_->GetSystemConfig().GetDownloadBehavior();
}

void SystemController::SetSystemDownloadBehavior(
    const std::string& download_behavior) {
  config_->GetSystemConfig().SetDownloadBehavior(download_behavior);
}

bool SystemController::GetSystemCacheLocalFiles() const {
  return config_->GetSystemConfig().GetCacheLocalFiles();
}

void SystemController::SetSystemCacheLocalFiles(bool cache_local_files) {
  config_->GetSystemConfig().SetCacheLocalFiles(cache_local_files);
}

bool SystemController::Run(const Logger& logger, bool enumerate_devices,
                           std::optional<int> device_id) const {
  // Custom progress handler for console output using percentages when
  // applicable
  auto progress_handler =
      std::make_unique<cli::ProgressTrackerHandler>(interrupt_);

  const auto& download_behaviour_option_value = GetSystemDownloadBehavior();

  cil::BenchmarkRunner::Params params;
  params.logger = loggerSystemController;
  params.progress_handler = progress_handler.get();
  params.config = config_.get();
  params.unpacker = unpacker_.get();
  params.ep_dependencies_manager = ep_dependencies_manager_.get();
  params.output_dir = output_dir_;
  params.data_dir = data_dir_;
  params.device_id = device_id;
  params.enumerate_only = enumerate_devices;
  params.output_results_schema_path = output_results_schema_path_;
  params.data_verification_file_schema_path =
      data_verification_file_schema_path_;
  params.input_file_schema_path = input_file_schema_path_;

  try {
    auto benchmark_runner = std::make_unique<cil::BenchmarkRunner>(params);

    const auto& scenarios = GetScenarios();

    std::chrono::steady_clock::time_point benchmark_start;

    benchmark_runner->AddCustomStage(
        "DisplayResult",
        [this, &benchmark_runner](const cil::ScenarioConfig&,
                                  struct cil::ScenarioData&,
                                  const cil::StageBase::ReportProgressCb&) {
          DisplayResultsStage(benchmark_runner->GetResultsLogger());
          return true;
        });

    using enum LogLevel;

    auto on_enter_stage_callback = [&logger, &scenarios,
                                    &download_behaviour_option_value, device_id,
                                    &enumerate_devices, &benchmark_start](
                                       const std::string_view& stage_name,
                                       int i) {
      if (stage_name == "Download") {
        if (enumerate_devices) {
          logger(kInfo, "");
        } else {
          logger(kInfo, "Starting to run scenario " + scenarios[i].GetName());
        }

        return true;
      }

      if (stage_name == "Preparation") {
        if (enumerate_devices) return true;
        if (download_behaviour_option_value == "deps_only") {
          logger(kInfo, "Downloaded dependencies successfully.");
          return enumerate_devices;
        }

        logger(kInfo, "Preparing for inferences execution...");

      } else if (stage_name == "DataVerification") {
        if (enumerate_devices) return false;
        logger(kInfo, "\nVerifying files...");
      } else if (stage_name == "Benchmark") {
        logger(kInfo, "");  // New line
        if (device_id.has_value()) {
          if (device_id.value() == -1) {
            logger(kInfo, "Running on all available devices");
          } else {
            logger(kInfo,
                   "Running on device " + std::to_string(device_id.value()));
          }
        }
        // Start the timer
        benchmark_start = std::chrono::high_resolution_clock::now();
      } else if (stage_name == "DisplayResult") {
        // Stop the timer and calculate the duration
        auto benchmark_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration =
            benchmark_end - benchmark_start;

        logger(kInfo, "\n\nFinished running of scenario " +
                          scenarios[i].GetName() + " in " +
                          std::to_string(duration.count()) + " seconds");
      }
      return true;
    };

    benchmark_runner->SetOnEnterStageCallback(on_enter_stage_callback);

    auto on_failed_stage_callback =
        [&logger](const std::string_view& stage_name, int) {
          if (stage_name == "Download") {
            logger(kError, "Failed to download necessary files, skipping...\n");
          } else if (stage_name == "Preparation") {
            logger(kError, "Inferences preparation failed, skipping...\n");
          } else if (stage_name == "DataVerification") {
            logger(kWarning, "Data verification failed, skipping...\n");

            // We continue the scenario even if files are not fully verified
            return true;
          } else if (stage_name == "Benchmark") {
            logger(kError, "Failed to run benchmarks, skipping...\n");
          }
          return false;
        };

    benchmark_runner->SetOnFailedStageCallback(on_failed_stage_callback);

    return benchmark_runner->Run();
  } catch (const std::exception& e) {
    std::string error_message = "Exception while running the benchmark: ";
    error_message += e.what();
    logger(LogLevel::kError, error_message);
    return false;
  }
}

void SystemController::InterruptHandler(int sig) {
  if (sig == SIGINT) {
    interrupt_ = true;
    std::signal(SIGINT, &SystemController::InterruptHandler);
  }
}

}  // namespace cli