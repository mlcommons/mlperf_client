#include "system_controller.h"

#include <log4cxx/logger.h>

#include <csignal>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <tuple>

#include "api_handler.h"
#include "benchmark_logger.h"
#include "ep_dependencies_manager.h"
#include "execution_config.h"
#include "execution_provider.h"
#include "executor_base.h"
#include "executor_factory.h"
#include "file_signature_verifier.h"
#include "progress_tracker.h"
#include "scenario_data_provider.h"
#include "storage.h"
#include "storage_task.h"
#include "task_scheduler.h"
#include "unpacker.h"
#include "url_cache_manager.h"
#include "utils.h"
#include "utils_directml.h"
#include "version.h"

using namespace log4cxx;
namespace fs = std::filesystem;
namespace utils = cil::utils;

const LoggerPtr loggerSystemController(Logger::getLogger("SystemController"));

namespace cli {

const std::vector<std::string> SystemController::kSupportedModels = {"llama2"};

SystemController::SystemController(const std::string& json_config_path,
                                   std::shared_ptr<cil::Unpacker> unpacker,
                                   const std::string& output_dir,
                                   const std::string& data_dir)
    : json_config_path_(json_config_path),
      unpacker_(unpacker),
      results_logger_(std::make_unique<cil::BenchmarkLogger>(output_dir)),
      data_dir_(data_dir.empty() ? "data" : data_dir) {
  std::signal(SIGINT, &SystemController::InterruptHandler);
}

SystemController::~SystemController() = default;

bool isConfigFileValid(const std::string& json_path, bool throw_error = true) {
  bool isValid = true;
  if (json_path.empty()) {
    if (throw_error) {
      LOG4CXX_ERROR(loggerSystemController,
                    "Config file path is missing.\nUse -h or --help for more "
                    "information.");
    }
    isValid = false;
  } else {
    fs::path json_file_path(json_path);
    if (!fs::exists(json_file_path) || !fs::is_regular_file(json_file_path)) {
      if (throw_error) {
        LOG4CXX_ERROR(
            loggerSystemController,
            "Config file does not exist or is not a file: " << json_path);
      }
      isValid = false;
    }
    // check if the file is a json file
    if (isValid && json_file_path.extension() != ".json") {
      if (throw_error) {
        LOG4CXX_ERROR(loggerSystemController,
                      "Config file is not a json file: " << json_path);
      }
      isValid = false;
    }
  }
  return isValid;
}

bool SystemController::ConfigStage() {
  using enum cil::Unpacker::Asset;

  if (!isConfigFileValid(json_config_path_, true)) {
    return false;
  }

  std::string config_schema_path;
  if (!unpacker_->UnpackAsset(kConfigSchema, config_schema_path)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack config schema, aborting...");
    return false;
  }

  config_ = std::make_unique<cil::ExecutionConfig>();
  bool json_loaded =
      config_->ValidateAndParse(json_config_path_, config_schema_path);

  if (!json_loaded) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Unable to load scenarios from config, aborting...");
    return false;
  }
  // Config validation step
  std::string config_verification_file_path;
  if (!unpacker_->UnpackAsset(kConfigVerificationFile,
                              config_verification_file_path)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack config verification file, aborting...");
    return false;
  }

  if (config_->IsConfigVerified(json_config_path_,
                                config_verification_file_path)) {
    config_verified_ = true;
    LOG4CXX_INFO(loggerSystemController, "Configuration tested by MLCommons");
  } else {
    config_verified_ = false;
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
            {ep.GetName(), GetEPParentLocation(ep).string()});
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

  // TODO: add download data size
  size_t required_data_space = unpacker_->GetAllDataSize();

  std::string deps_dir = unpacker_->GetDepsDir();
  size_t disc_space = utils::GetAvailableDiskSpace(deps_dir);
  if (disc_space == -1) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Can not determine available disc space, aborting...");
    return false;
  } else {
    // require some more extra space for other activities
    size_t low_space_threshold = 100LL * 1024 * 1024;
    size_t required_space = required_data_space + low_space_threshold;
    if (disc_space < required_space) {
      std::cerr
          << "At least " << required_space / 1e6
          << "mb of available space is required for EPs unpacking, currently "
          << std::fixed << disc_space / 1e6 << "mb is available, aborting..."
          << std::endl;
      return false;
    }
  }
  return true;
}

bool SystemController::DownloadStage(int scenario_index,
                                     const std::string& download_behavior) {
  bool dependencies_only = download_behavior == "deps_only";
  const auto& scenarios = config_->GetScenarios();
  if (scenario_index < 0 || scenario_index >= scenarios.size()) return false;

  const auto& scenario = scenarios[scenario_index];

  bool retrieve_results_file = !scenario.GetResultsFile().empty();
  if (retrieve_results_file) {
    if (utils::FileExists(scenario.GetResultsFile())) {
      // if the file is already downloaded, no need to download it again
      output_results_file_paths_[scenario_index] = scenario.GetResultsFile();
      retrieve_results_file = false;
      LOG4CXX_INFO(loggerSystemController, "Results Verification File "
                                               << scenario.GetResultsFile()
                                               << " is already downloaded");
    }
  } else {
    LOG4CXX_INFO(
        loggerSystemController,
        "Results Verification File is not set, the default file will be used");
  }

  cil::ProgressTracker storage_tasks_progress_tracker(
      0, "download", std::chrono::milliseconds(200));

  cil::TaskScheduler storage_tasks_scheduler("Download Scheduler");

  using StoragePtr = std::shared_ptr<cil::Storage>;
  using StorageTaskPtr = std::shared_ptr<cil::StorageTask>;
  using StorageTaskVector = std::vector<StorageTaskPtr>;
  using StorageVector = std::vector<StoragePtr>;

  std::hash<std::string> hasher;

  const bool force_download = download_behavior == "forced";

  auto addStorageTasks =
      [&](StoragePtr storage, const auto& paths, StorageTaskVector& tasks,
          const std::string& model_name, bool use_hash = false) {
        for (const auto& [file_path, file_key] : paths) {
          std::string final_dir = model_name;
          if (use_hash) final_dir += "/" + std::to_string(hasher(file_key));
          auto storage_task =
              std::make_shared<cil::StorageTask>(storage, file_path, final_dir);
          tasks.push_back(storage_task);
          std::string task_name = "Download " + file_path;
          storage_tasks_scheduler.ScheduleTask(
              task_name, [storage_task]() { (*storage_task)(); });
          storage_tasks_progress_tracker.AddTask(storage_task);
        }
      };

  auto cancelStorageTasks = [&](const StorageTaskVector& tasks) {
    for (auto task : tasks) task->Cancel();
  };

  auto containerToMap = [](const auto& input) {
    std::unordered_map<std::string, std::string> output;
    for (const auto& key : input) {
      output[key] = "";
    }
    return output;
  };

  auto containerToVector = [](const auto& input) {
    std::vector<std::pair<std::string, std::string>> output;
    for (const auto& key : input) {
      output.emplace_back(key, "");
    }
    return output;
  };

  auto url_cache_manager = std::make_shared<cil::URLCacheManager>();

  auto data_storage = std::make_shared<cil::Storage>(
      data_dir_, url_cache_manager, force_download);

  StorageTaskVector model_files_storage_tasks;
  StorageTaskVector model_ext_files_storage_tasks;
  StorageTaskVector input_storage_tasks;
  StorageTaskVector assets_storage_tasks;
  StorageTaskVector other_storage_tasks;
  StorageTaskVector ep_dependencies_storage_tasks;

  if (!dependencies_only) {
    addStorageTasks(data_storage, scenario.GetModelFiles(),
                    model_files_storage_tasks, scenario.GetName(), true);
    addStorageTasks(data_storage, scenario.GetModelExtraFiles(),
                    model_ext_files_storage_tasks, scenario.GetName(), true);
    addStorageTasks(data_storage, containerToVector(scenario.GetInputs()),
                    input_storage_tasks, scenario.GetName());
    addStorageTasks(data_storage, containerToMap(scenario.GetAssets()),
                    assets_storage_tasks, scenario.GetName());
  }

  StorageVector ep_dependencies_storages;
  // loop over the execution providers and download the dependencies
  for (const auto& ep : scenario.GetExecutionProviders()) {
    if (!cil::utils::IsEpSupportedOnThisPlatform(scenario.GetName(),
                                                 ep.GetName()))
      continue;

    const auto& ep_external_dependencies = ep.GetDependencies();
    if (ep_external_dependencies.empty()) continue;

    // create path variable to store the destination directory
    fs::path dest_dir = GetEPParentLocation(ep);

    ep_dependencies_storages.push_back(std::make_shared<cil::Storage>(
        dest_dir.string(), url_cache_manager, force_download));
    addStorageTasks(ep_dependencies_storages.back(),
                    containerToMap(ep_external_dependencies),
                    ep_dependencies_storage_tasks, "");
  }

  if (retrieve_results_file && !dependencies_only)
    addStorageTasks(
        data_storage,
        std::map<std::string, std::string>{{scenario.GetResultsFile(), ""}},
        other_storage_tasks, scenario.GetName());

  // Internal EPs dependencies
  const auto& ep_storage_files = ep_dependencies_manager_->GetEpsStorageFiles();
  for (const auto& [ep_deps_dir, files] : ep_storage_files) {
    ep_dependencies_storages.push_back(std::make_shared<cil::Storage>(
        ep_deps_dir, url_cache_manager, force_download, true));
    addStorageTasks(ep_dependencies_storages.back(), containerToMap(files),
                    other_storage_tasks, "");
  }
  int total_num_tasks = storage_tasks_progress_tracker.GetTaskCount();
  if (download_behavior == "skip_all" || download_behavior == "prompt") {
    for (auto& task : storage_tasks_progress_tracker.GetTasks()) {
      if (!task->CheckIfTaskCanBeSkipped()) {
        if (download_behavior == "skip_all") {
          LOG4CXX_ERROR(loggerSystemController,
                        task->GetDescription()
                            << " Can not be skipped, the file does not exist");
          return false;
        } else if (download_behavior == "prompt") {
          std::string user_input;
          std::cout << task->GetDescription()
                    << " Can not be skipped, the file does not exist, Do you "
                       "want to download it? (y/n): ";
          std::cin >> user_input;
          if (user_input != "y") {
            LOG4CXX_ERROR(loggerSystemController,
                          "Download Stage interrupted, stopping...");
            return false;
          }
        }
      } else {
        total_num_tasks--;
      }
    }
    // Remove skipped tasks from the tracker
    storage_tasks_progress_tracker.RemoveSkippedTasks();
    if (total_num_tasks == 0) {
      LOG4CXX_INFO(loggerSystemController, "Downloading files skipped.");
    }
  }

  if (total_num_tasks > 0) {
    LOG4CXX_INFO(loggerSystemController, "\nDownloading necessary files...\n");
    storage_tasks_progress_tracker.StartTracking();
    storage_tasks_scheduler.ExecuteTasksAsync();
    interrupt_ = false;
    while (!storage_tasks_progress_tracker.Finished()) {
      storage_tasks_progress_tracker.Update();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (interrupt_) {
        if (storage_tasks_progress_tracker.RequestInterrupt()) {
          storage_tasks_scheduler.CancelTasks();
          cancelStorageTasks(model_files_storage_tasks);
          cancelStorageTasks(model_ext_files_storage_tasks);
          cancelStorageTasks(input_storage_tasks);
          cancelStorageTasks(assets_storage_tasks);
          cancelStorageTasks(other_storage_tasks);
          cancelStorageTasks(ep_dependencies_storage_tasks);
          LOG4CXX_INFO(loggerSystemController,
                       "\nModel "
                           << scenario.GetName()
                           << ", Download Stage interrupted, stopping...");
          return false;
        } else {
          interrupt_ = false;
        }
      }
    }
    storage_tasks_progress_tracker.StopTracking();
    storage_tasks_scheduler.Join();
  }

  for (const auto& task : storage_tasks_progress_tracker.GetTasks()) {
    if (task->GetStatus() == cil::ProgressableTask::Status::kFailed) {
      LOG4CXX_ERROR(loggerSystemController,
                    "Download Stage failed, stopping...");
      return false;
    }
  }

  bool unpacking_files = false;

  auto unpackIfNecessary =
      [&](const StorageTaskPtr& task) -> std::vector<std::string> {
    std::filesystem::path file_path = task->GetResPath();
    if (!fs::exists(file_path)) return {};

    std::vector<std::string> files;
    if (file_path.extension() == ".zip" || file_path.extension() == ".ZIP") {
      if (!unpacking_files) {
        LOG4CXX_INFO(loggerSystemController, "Unpacking downloaded files...");
        unpacking_files = true;
      }

      files = cil::Unpacker::UnpackFilesFromZIP(
          file_path.string(), file_path.parent_path().string());
    }

    if (files.empty()) files.push_back(file_path.string());

    return files;
  };

  auto retrievePathsFromTasks = [&](const StorageTaskVector& tasks,
                                    std::vector<std::string>& paths) {
    for (const auto& task : tasks) {
      auto files = unpackIfNecessary(task);
      if (files.empty()) return false;

      for (auto& file : files) {
        file = fs::relative(utils::NormalizePath(file)).string();
        source_to_path_map_[task->GetSourcePath()] = file;
        path_to_source_map_[file] = task->GetSourcePath();
        paths.push_back(file);
      }
    }
    return true;
  };

  if (!retrievePathsFromTasks(model_files_storage_tasks,
                              model_file_paths_[scenario_index]))
    return false;
  if (!retrievePathsFromTasks(model_ext_files_storage_tasks,
                              asset_file_paths_[scenario_index]))
    return false;
  if (!retrievePathsFromTasks(input_storage_tasks,
                              input_file_paths_[scenario_index]))
    return false;
  if (!retrievePathsFromTasks(assets_storage_tasks,
                              asset_file_paths_[scenario_index]))
    return false;

  for (const auto& task : other_storage_tasks) {
    if (unpackIfNecessary(task).empty()) return false;
  }

  if (retrieve_results_file)
    output_results_file_paths_[scenario_index] =
        other_storage_tasks.front()->GetResPath();

  return true;
}

bool SystemController::PreparationStage(int scenario_index) {
  const auto& scenarios = config_->GetScenarios();
  if (scenario_index < 0 || scenario_index >= scenarios.size()) return false;

  const auto& scenario = scenarios[scenario_index];

  const auto& eps = scenario.GetExecutionProviders();
  int failsCount = 0;
  std::unordered_set<std::string> failed_ep_names;
  std::unordered_set<std::string> prepared_ep_names;
  prepared_eps_.clear();

  bool native_openvino_unpacked = false;
  bool ort_genai_unpacked = false;

  using enum cil::Unpacker::Asset;

  std::string ort_genai_library_path;

  for (const auto& ep : eps) {
    const auto& ep_name = ep.GetName();
    auto library_path = ep.GetLibraryPath();
    bool empty_library_path = library_path.empty();

    bool was_visited = failed_ep_names.contains(ep_name);
    bool is_supported =
        cil::utils::IsEpSupportedOnThisPlatform(scenario.GetName(), ep_name);

    try {
      if (!is_supported ||
          empty_library_path &&
              !ep_dependencies_manager_->PrepareDependenciesForEP(ep_name)) {
        if (is_supported) {
          throw std::runtime_error("Failed to prepare " + ep_name +
                                   "dependencies, skipping...");
        } else {
          throw std::runtime_error(ep_name + " is not supported, skipping...");
        }
      }

      // We copy the config to be able to modify it
      // Regardless if the config will be altered, we will override the EP
      // config with this new one
      auto ep_config_json = ep.GetConfig();

      if (!empty_library_path) {
        if (!fs::exists(library_path)) {
          std::string error =
              "IHV library path does not exist: " + library_path;
          throw std::runtime_error(error);
        }
      } else {
        auto unpackIfNeeded =
            [&](const std::vector<
                std::pair<cil::Unpacker::Asset, std::reference_wrapper<bool>>>&
                    assets) {
              bool unpacked = true;
              for (auto& [asset, single_unpacked] : assets) {
                if (!unpacker_->UnpackAsset(asset, library_path,
                                            !single_unpacked)) {
                  unpacked = false;
                  break;
                } else
                  single_unpacked.get() = true;
              }
              if (!unpacked) {
                throw std::runtime_error("Failed to unpack " + ep_name +
                                         " library");
              }
              if (auto error = cil::API_Handler::CanBeLoaded(
                      cil::API_Handler::Type::kIHV, library_path);
                  !error.empty()) {
                throw std::runtime_error(
                    "Failed to load required libraries for " + ep_name + "\n" +
                    error);
              }
            };

        switch (cil::NameToEP(ep_name)) {
#if WITH_IHV_NATIVE_OPENVINO
          case cil::EP::kIHVNativeOpenVINO:
            unpackIfNeeded({{kNativeOpenVINO, native_openvino_unpacked}});
            break;
#endif
#if WITH_IHV_ORT_GENAI
          case cil::EP::kIHVOrtGenAI:
            unpackIfNeeded({{kOrtGenAI, ort_genai_unpacked}});
            if (ort_genai_unpacked) {
              ort_genai_library_path = library_path;
            }
            break;
#endif
          default:
            throw std::runtime_error("Unrecognized internal IHV EP: " +
                                     ep_name);
        }
      }

      prepared_ep_names.insert(ep_name);
      prepared_eps_.emplace_back(ep.OverrideConfig(ep_config_json),
                                 library_path);
    } catch (const std::exception& e) {
      if (!was_visited) LOG4CXX_ERROR(loggerSystemController, e.what());
      failed_ep_names.insert(ep_name);
      ++failsCount;
    }
  }

#if defined(WIN32)

  auto check_directml_compatible_ep = [&](const std::string& ep_name,
                                          const std::string& library_path,
                                          bool update_config) {
    if (!PrepareDirectMLAdaptersIfNeeded(failed_ep_names, ep_name, library_path,
                                         update_config)) {
      prepared_ep_names.erase(ep_name);
    }
  };

  if (ort_genai_unpacked) {
    check_directml_compatible_ep("OrtGenAI", ort_genai_library_path, true);
  }
#endif

  std::stringstream not_prepared_eps_str;
  for (const auto& ep : failed_ep_names) {
    if (prepared_ep_names.contains(ep)) continue;

    if (std::streampos(0) != not_prepared_eps_str.tellp()) {
      not_prepared_eps_str << ", ";
    }
    not_prepared_eps_str << ep;
  }
  if (auto str = not_prepared_eps_str.str(); !str.empty())
    LOG4CXX_INFO(loggerSystemController,
                 "Following EPs used for the model "
                     << scenario.GetName()
                     << " could not be prepared and will not be "
                        "used for inference: ["
                     << not_prepared_eps_str.str() << "].\n");

  if (failsCount == eps.size() || eps.empty()) return false;

  if (!output_results_file_paths_.contains(scenario_index)) {
    output_results_file_paths_[scenario_index] = "";
    LOG4CXX_INFO(
        loggerSystemController,
        "Results Verification File is not set, verification is disabled");
  }

  std::string data_verification_file_path = scenario.GetDataVerificationFile();
  if (data_verification_file_path.empty() &&
      !unpacker_->UnpackAsset(kDataVerificationFile,
                              data_verification_file_path)) {
    LOG4CXX_ERROR(loggerSystemController,
                  "Failed to unpack data verification file, aborting...");
    return false;
  }

  scenario_data_providers_[scenario_index] =
      std::make_shared<cil::ScenarioDataProvider>(
          asset_file_paths_[scenario_index], input_file_paths_[scenario_index],
          input_file_schema_path_, output_results_file_paths_[scenario_index],
          output_results_schema_path_);

  file_signature_verifiers_[scenario_index] =
      std::make_shared<cil::FileSignatureVerifier>(
          data_verification_file_path, data_verification_file_schema_path_);
  return true;
}

fs::path SystemController::GetEPParentLocation(
    const cil::ExecutionProviderConfig& ep_config) const {
  const auto& ep_name = ep_config.GetName();

  auto library_path = ep_config.GetLibraryPath();

  // create path variable to store the destination directory
  fs::path dest_dir;
  if (!library_path.empty()) {
    // Remove the file:// prefix if it exists
    if (library_path.find("file://") == 0) {
      library_path = library_path.substr(7);
    }
    dest_dir = fs::path(library_path).parent_path();
  } else {
    dest_dir = fs::path(unpacker_->GetDepsDir()) / "IHV" / ep_name;
  }

  return dest_dir;
}

bool SystemController::PrepareDirectMLAdaptersIfNeeded(
    std::unordered_set<std::string>& not_prepared_eps,
    const std::string_view& target_ep_name,
    const std::string_view& ep_library_path, bool update_config) {
#ifdef _WIN32
  // Extract parent path from library_path
  if (ep_library_path.empty()) return false;

  auto parent_path = fs::path(ep_library_path).parent_path().string();

  auto adapters = utils::DirectML::EnumerateAdapters(parent_path);

  int valid_eps = 0;

  // Now we need to loop though all EPs and check if DirectML is used
  // If it's used, we can check the device_id, and mark as used
  // If the device_id is not found, we can set it manually based on adapters
  // not used yet If device_type was specified, we ignore specific adapters,
  std::vector<bool> adapters_used(adapters.size(), false);

  // loop though each adapter, and if type is emtpy, we mark as used, to omit
  // in the next step
  for (size_t i = 0; i < adapters.size(); i++) {
    if (adapters[i].type.empty()) adapters_used[i] = true;
  }

  for (const auto& [ep, library_path] : prepared_eps_) {
    if (ep.GetName() != target_ep_name) {
      continue;
    }

    const auto& ep_config = ep.GetConfig();
    int ep_device_id = -1;

    //  Obtain values if exists
    if (!ep_config.contains("device_id")) continue;

    ep_device_id = ep_config["device_id"];

    if (ep_device_id >= 0 && ep_device_id < adapters_used.size()) {
      adapters_used[ep_device_id] = true;
    }
  }

  // We need to create a new list of EPs, with new copied ones in correct
  // order This EPs will have device_id set manually, depending on the
  // adapters availability
  std::vector<std::pair<cil::ExecutionProviderConfig, std::string>>
      prepared_eps;

  for (const auto& [ep, library_path] : prepared_eps_) {
    const auto& ep_name = ep.GetName();

    if (ep_name != target_ep_name) {
      prepared_eps.emplace_back(ep, library_path);
      continue;
    }

    auto ep_config = ep.GetConfig();
    int device_id = -1;
    std::string device_type;
    std::string device_vendor;

    //  Obtain values again if exists

    if (ep_config.contains("device_type")) {
      device_type = ep_config["device_type"];
    }

    if (ep_config.contains("device_vendor")) {
      device_vendor = utils::StringToLowerCase(ep_config["device_vendor"]);
    }

    if (ep_config.contains("device_id")) {
      device_id = ep_config["device_id"];
      if (device_id >= 0) {
        if (device_id >= adapters.size()) {
          LOG4CXX_ERROR(loggerSystemController,
                        "Failed to prepare "
                            << target_ep_name
                            << " EP, device_id is out of range");
        } else if (!device_type.empty() &&
                   device_type != adapters[device_id].type) {
          LOG4CXX_ERROR(loggerSystemController,
                        "Failed to prepare " << target_ep_name
                                             << " EP, device type does not "
                                                "match the adapter type");
        } else if (!device_vendor.empty() &&

                   utils::StringToLowerCase(adapters[device_id].name)
                           .find(device_vendor) == std::string::npos) {
          LOG4CXX_ERROR(loggerSystemController,
                        "Failed to prepare " << target_ep_name
                                             << " EP, device vendor does not "
                                                "match the adapter vendor");
        } else if (update_config) {
          ep_config["device_id"] = device_id;
          ep_config["device_name"] = adapters[device_id].name;
          ep_config["device_type"] = adapters[device_id].type;
          ep_config["luid_l"] = adapters[device_id].luid_low_part;
          ep_config["luid_h"] = adapters[device_id].luid_high_part;

          auto newEP = ep.OverrideConfig(ep_config);

          prepared_eps.emplace_back(newEP, library_path);
          valid_eps++;
        } else {
          prepared_eps.emplace_back(ep, library_path);
          valid_eps++;
        }
        continue;
      }
    }

    bool found = false;

    // Each time we check available adapters, this EP might have different
    // settings or duplicate

    auto adapters_used_copy = adapters_used;

    for (size_t i = 0; i < adapters.size(); i++) {
      if (!adapters_used_copy[i] &&
          (device_type.empty() || device_type == adapters[i].type) &&
          (device_vendor.empty() ||
           utils::StringToLowerCase(adapters[i].name).find(device_vendor) !=
               std::string::npos)) {
        ep_config["device_id"] = i;
        ep_config["device_name"] = adapters[i].name;
        ep_config["device_type"] = adapters[i].type;
        if (!device_vendor.empty())
          ep_config["device_vendor"] = ep_config["device_vendor"];
        ep_config["luid_l"] = adapters[i].luid_low_part;
        ep_config["luid_h"] = adapters[i].luid_high_part;
        adapters_used_copy[i] = true;
        found = true;

        auto newEP = ep.OverrideConfig(ep_config);

        // Create copy of the EP with new device_id
        prepared_eps.emplace_back(newEP, library_path);
        valid_eps++;
      }
    }

    if (!found) {
      if (!device_type.empty() && !device_vendor.empty())
        LOG4CXX_ERROR(loggerSystemController,
                      "Failed to prepare "
                          << target_ep_name
                          << ", no available adapters found for "
                             "device_type: "
                          << device_type << " and device_vendor: "
                          << ep_config["device_vendor"]);
      else if (!device_type.empty())
        LOG4CXX_ERROR(loggerSystemController,
                      "Failed to prepare "
                          << target_ep_name
                          << ", no available adapters found for "
                             "device type: "
                          << device_type);
      else if (!device_vendor.empty())
        LOG4CXX_ERROR(loggerSystemController,
                      "Failed to prepare "
                          << target_ep_name
                          << ", no available adapters found for "
                             "device_vendor: "
                          << ep_config["device_vendor"]);

      not_prepared_eps.insert(ep_name);
    }
  }

  prepared_eps_ = prepared_eps;

  return valid_eps > 0;
#endif  // _WIN32
  return false;
}

bool SystemController::DataVerificationStage(int model_index) {
  auto verifier = file_signature_verifiers_[model_index];
  std::string hash;
  std::map<std::string, std::string> not_verified_files;

  for (const auto& model_file_path : model_file_paths_[model_index]) {
    if (!verifier->Verify(model_file_path, hash))
      not_verified_files[model_file_path] = hash;
  }
  for (const auto& input_file_path : input_file_paths_[model_index]) {
    if (!verifier->Verify(input_file_path, hash))
      not_verified_files[input_file_path] = hash;
  }

  if (not_verified_files.empty()) return true;

  LOG4CXX_WARN(loggerSystemController,
               "Following files used for the model "
                   << config_->GetScenarios()[model_index].GetName()
                   << " cannot be verified, using at your risk:");
  for (const auto& [file_path, hash] : not_verified_files)
    LOG4CXX_WARN(
        loggerSystemController,
        "[" << utils::GetFileNameFromPath(file_path) << ": " << hash << "]");

  return false;
}

bool SystemController::BenchmarkStage(int scenario_index) {
  const auto& scenarios = config_->GetScenarios();
  if (scenario_index < 0 || scenario_index >= scenarios.size()) return false;

  const auto& scenario = scenarios[scenario_index];
  const auto& execution_providers = prepared_eps_;

  std::optional<std::string> last_model_name;
  std::string display_model_name;

  std::set<std::string> executed_models_hashes;

  std::vector<
      std::tuple<cil::TaskScheduler, cil::ProgressTracker,
                 std::vector<std::shared_ptr<cil::infer::ExecutorBase>>>>
      benchmarks;

  bool task_executed = false;  // true if least 1 task is executed
  for (const auto& model_file_path : model_file_paths_[scenario_index]) {
    const std::string& model_source_path = path_to_source_map_[model_file_path];
    cil::ModelConfig model_config =
        scenario.GetModelByFilePath(model_source_path);

    const auto& model_name = model_config.GetName();

    std::string model_hash =
        utils::ComputeFileSHA256(model_file_path, loggerSystemController);
    if (!executed_models_hashes.empty() &&
        executed_models_hashes.contains(model_hash)) {
      LOG4CXX_INFO(loggerSystemController,
                   "Model: " << model_source_path
                             << " was already executed, skipping...");

      continue;
    }
    executed_models_hashes.insert(model_hash);

    unsigned new_executors_count = 0;

    std::stringstream eps_description_stream;
    for (auto& [ep, library_path] : execution_providers) {
      if (model_name.empty()) {
        // This is overriden scenario source path, check if the current EP
        // uses it
        if (!ep.HasModelFileConfig(model_source_path)) continue;

        display_model_name = ep.GetModelByFilePath(model_source_path).GetName();

      } else {
        if (ep.HasModelConfig(model_name)) {
          // This EP is overriding the scenario, so we skip its execution using
          // the global scenario
          continue;
        }
        display_model_name = model_name;
      }

      if (!last_model_name.has_value() ||
          last_model_name != display_model_name) {
        benchmarks.emplace_back(
            cil::TaskScheduler("Benchmark Scheduler - " + scenario.GetName()),
            cil::ProgressTracker(0, "benchmark"),
            std::vector<std::shared_ptr<cil::infer::ExecutorBase>>{});

        last_model_name = display_model_name;
      }

      auto& [scheduler, progress_tracker, executors] = benchmarks.back();

      nlohmann::json ep_config = ep.GetConfig();
      const auto& ep_name = ep.GetName();

      std::string tokenizer_path =
          ep.GetModelByFilePath(model_source_path).GetTokenizerPath();
      // if tokenizer_path is empty, it means this EP is not overriding it
      if (tokenizer_path.empty())
        tokenizer_path = model_config.GetTokenizerPath();

      scenario_data_providers_[scenario_index]->SetLlama2TokenizerPath(
          source_to_path_map_[tokenizer_path]);

      auto executor = cil::infer::ExecutorFactory::Create(
          scenario.GetName(), model_file_path,
          scenario_data_providers_[scenario_index], library_path, ep_name,
          ep_config, scenario.GetIterations(), scenario.GetIterationsWarmUp(),
          scenario.GetInferenceDelay());

      bool is_ortgenai = ep_name == "OrtGenAI";
      if (is_ortgenai) {
        // we don't need Adapter LUID in the results and console
        ep_config.erase("luid_l");
        ep_config.erase("luid_h");
      }
      if (!executor) {
        LOG4CXX_ERROR(loggerSystemController,
                      "Failed to run benchmark for "
                          << scenario.GetName() << " model \""
                          << display_model_name
                          << "\", model file: " << model_file_path
                          << ", EP: " << ep_name << ep_config);
        continue;
      }
      eps_description_stream
          << (eps_description_stream.str().empty() ? "" : ", ") << ep_name
          << ep_config;

      std::string task_name = "Benchmark " + ep_name;
      scheduler.ScheduleTask(task_name, [=, &task_executed]() {
        LOG4CXX_INFO(loggerSystemController,
                     "\nmodel "
                         << scenario.GetName()
                         << ", Preparing to run the benchmark for the EP "
                         << ep_name << ep_config);

        auto start_time = std::chrono::high_resolution_clock::now();

        (*executor)();
        task_executed = true;

        cil::BenchmarkResult res = executor->GetBenchmarkResult();
        results_logger_->SetConfigVerified(config_verified_);
        results_logger_->SetApplicationVersionString(
            std::string(APP_VERSION_STRING) + " " +
            std::string(APP_BUILD_NAME));
        res.model_name = display_model_name;
        res.config_file_name = config_->GetConfigFileName();
        res.config_file_hash = config_->GetConfigFileHash();
        res.model_path = model_source_path;
        LOG4CXX_INFO(loggerSystemController,
                     "Model " << scenario.GetName()
                              << ", model path Result: " << res.model_path);
        res.asset_paths = scenario.GetAssets();
        res.data_paths = scenario.GetInputs();
        res.results_file = scenario.GetResultsFile();
        auto end_time = std::chrono::high_resolution_clock::now();
        res.benchmark_duration = utils::FormatDuration(end_time - start_time);
        if (is_ortgenai) res.ep_configuration = ep_config;
        try {
          bool passed = results_logger_->AppendBenchmarkResult(res);
          if (!passed)
            LOG4CXX_ERROR(loggerSystemController,
                          "Failed to append benchmark result");
        } catch (const std::exception& e) {
          LOG4CXX_ERROR(loggerSystemController,
                        "Failed to append benchmark result: " << e.what());
        }
      });
      progress_tracker.AddTask(executor);
      executors.push_back(executor);
      new_executors_count++;
    }

    auto original_name = path_to_source_map_[model_file_path];

    if (new_executors_count)
      LOG4CXX_INFO(
          loggerSystemController,
          "Running benchmarks for EPs "
              << eps_description_stream.str() << " on model \""
              << (display_model_name.empty() ? model_name : display_model_name)
              << "\" (" << original_name << " -> " << model_file_path
              << "), added " << new_executors_count << " task(s)\n");
    else
      LOG4CXX_ERROR(loggerSystemController,
                    "No valid EPs found for model "
                        << (model_name.empty() ? "" : "\"" + model_name + "\" ")
                        << "(" << original_name << " -> " << model_file_path
                        << ")\n");
  }

  if (execution_providers.empty()) {
    // show error message if no valid EPs found for the model
  }

  for (auto& [scheduler, progress_tracker, executors] : benchmarks) {
    progress_tracker.StartTracking();

    scheduler.ExecuteTasksAsync();

    interrupt_ = false;
    while (!progress_tracker.Finished()) {
      progress_tracker.Update();
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      if (interrupt_) {
        if (progress_tracker.RequestInterrupt()) {
          scheduler.CancelTasks();
          for (auto executor : executors) executor->Cancel();
          LOG4CXX_INFO(loggerSystemController,
                       "\nModel "
                           << scenario.GetName()
                           << ", Benchmark Stage interrupted, stopping...");
          return false;
        } else
          interrupt_ = false;
      }
    }
    // Wait for the inferences to finish
    scheduler.Join();
    progress_tracker.StopTracking();
  }

  return task_executed;
}

void SystemController::DisplayResultsStage() {
  results_logger_->DisplayAllResults();
  results_logger_->Clear();
}

const std::vector<cil::ScenarioConfig>& SystemController::GetScenarios() const {
  return config_->GetScenarios();
}

const std::string& SystemController::GetSystemConfigTempPath() const {
  return config_->GetSystemConfig().GetTempPath();
}

const std::string& SystemController::GetDownloadBehavior() const {
  return config_->GetSystemConfig().GetDownloadBehavior();
}

std::atomic<bool> SystemController::interrupt_(false);
void SystemController::InterruptHandler(int sig) {
  if (sig == SIGINT) {
    interrupt_ = true;
    std::signal(SIGINT, &SystemController::InterruptHandler);
  }
}

}  // namespace cli
