#include "download_stage.h"

#include <iostream>

#include "storage.h"
#include "storage_task.h"
#include "task_scheduler.h"
#include "url_cache_manager.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {
const std::chrono::milliseconds DownloadStage::kProgressInterval =
    std::chrono::milliseconds(200);

bool DownloadStage::Run(const ScenarioConfig& scenario_config,
                        ScenarioData& scenario_data,
                        const ReportProgressCb& raport_progress_cb) {
  auto download_behavior = config_.GetSystemConfig().GetDownloadBehavior();
  auto cache_local_files = config_.GetSystemConfig().GetCacheLocalFiles();

  bool dependencies_only = download_behavior == "deps_only" ||
                           download_behavior == "deps_only_enumeration";

  bool retrieve_results_file = !scenario_config.GetResultsFile().empty();
  if (retrieve_results_file) {
    if (utils::FileExists(scenario_config.GetResultsFile())) {
      // if the file is already downloaded, no need to download it again
      scenario_data.output_results_file_paths =
          scenario_config.GetResultsFile();
      retrieve_results_file = false;
      LOG4CXX_INFO(logger_, "Results Verification File "
                                << scenario_config.GetResultsFile()
                                << " is already downloaded");
    }
  } else {
    LOG4CXX_INFO(
        logger_,
        "Results Verification File is not set, the default file will be used");
  }

  ProgressTracker storage_tasks_progress_tracker(0, "download",
                                                 kProgressInterval);

  TaskScheduler storage_tasks_scheduler("Download Scheduler");

  using StoragePtr = std::shared_ptr<Storage>;
  using StorageTaskPtr = std::shared_ptr<StorageTask>;
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
          auto storage_task = std::make_shared<StorageTask>(
              storage, file_path, final_dir, cache_local_files);
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

  auto url_cache_manager = std::make_shared<URLCacheManager>();

  auto data_storage =
      std::make_shared<Storage>(data_dir_, url_cache_manager, force_download);

  StorageTaskVector model_files_storage_tasks;
  StorageTaskVector model_ext_files_storage_tasks;
  StorageTaskVector input_storage_tasks;
  StorageTaskVector assets_storage_tasks;
  StorageTaskVector other_storage_tasks;
  StorageTaskVector ep_dependencies_storage_tasks;

  if (!dependencies_only) {
    addStorageTasks(data_storage, scenario_config.GetModelFiles(),
                    model_files_storage_tasks, scenario_config.GetName(), true);
    addStorageTasks(data_storage, scenario_config.GetModelExtraFiles(),
                    model_ext_files_storage_tasks, scenario_config.GetName(),
                    true);
    addStorageTasks(data_storage,
                    containerToVector(scenario_config.GetInputs()),
                    input_storage_tasks, scenario_config.GetName());
    addStorageTasks(data_storage, containerToMap(scenario_config.GetAssets()),
                    assets_storage_tasks, scenario_config.GetName());
  }

  StorageVector ep_dependencies_storages;
  // loop over the execution providers and download the dependencies
  for (const auto& ep : scenario_config.GetExecutionProviders()) {
    if (!utils::IsEpSupportedOnThisPlatform(scenario_config.GetName(),
                                            ep.GetName()))
      continue;

    const auto& ep_external_dependencies = ep.GetDependencies();
    if (ep_external_dependencies.empty()) continue;

    // create path variable to store the destination directory
    fs::path dest_dir =
        GetExecutionProviderParentLocation(ep, unpacker_.GetDepsDir());

    ep_dependencies_storages.push_back(std::make_shared<Storage>(
        dest_dir.string(), url_cache_manager, force_download));
    addStorageTasks(ep_dependencies_storages.back(),
                    containerToMap(ep_external_dependencies),
                    ep_dependencies_storage_tasks, "");
  }

  if (retrieve_results_file && !dependencies_only)
    addStorageTasks(data_storage,
                    std::map<std::string, std::string>{
                        {scenario_config.GetResultsFile(), ""}},
                    other_storage_tasks, scenario_config.GetName());

  // Internal EPs dependencies
  const auto& ep_storage_files = ep_dependencies_manager_.GetEpsStorageFiles();
  for (const auto& [ep_deps_dir, files] : ep_storage_files) {
    ep_dependencies_storages.push_back(std::make_shared<Storage>(
        ep_deps_dir, url_cache_manager, force_download, true));
    addStorageTasks(ep_dependencies_storages.back(), containerToMap(files),
                    other_storage_tasks, "");
  }
  int total_num_tasks = storage_tasks_progress_tracker.GetTaskCount();
  if (download_behavior == "skip_all" || download_behavior == "prompt" ||
      download_behavior == "deps_only_enumeration") {
    for (auto& task : storage_tasks_progress_tracker.GetTasks()) {
      if (!task->CheckIfTaskCanBeSkipped()) {
        if (download_behavior == "skip_all") {
          LOG4CXX_ERROR(logger_,
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
            LOG4CXX_ERROR(logger_, "Download Stage interrupted, stopping...");
            return false;
          }
        }
      } else {
        total_num_tasks--;
      }
    }
    // Remove skipped tasks from the tracker
    storage_tasks_progress_tracker.RemoveSkippedTasks();
    if (total_num_tasks == 0 && download_behavior != "deps_only_enumeration") {
      LOG4CXX_INFO(logger_, "Downloading files skipped.");
    }
  }

  if (total_num_tasks > 0) {
    LOG4CXX_INFO(logger_, "\nDownloading necessary files...\n");

    if (!raport_progress_cb(storage_tasks_progress_tracker,
                            storage_tasks_scheduler)) {
      cancelStorageTasks(model_files_storage_tasks);
      cancelStorageTasks(model_ext_files_storage_tasks);
      cancelStorageTasks(input_storage_tasks);
      cancelStorageTasks(assets_storage_tasks);
      cancelStorageTasks(other_storage_tasks);
      cancelStorageTasks(ep_dependencies_storage_tasks);
      LOG4CXX_INFO(logger_, "\nModel "
                                << scenario_config.GetName()
                                << ", Download Stage interrupted, stopping...");
      return false;
    }
  }

  for (const auto& task : storage_tasks_progress_tracker.GetTasks()) {
    if (task->GetStatus() == ProgressableTask::Status::kFailed) {
      LOG4CXX_ERROR(logger_, "Download Stage failed, stopping...");
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
        LOG4CXX_INFO(logger_, "Unpacking downloaded files...");
        unpacking_files = true;
      }

      files = Unpacker::UnpackFilesFromZIP(file_path.string(),
                                           file_path.parent_path().string());

      // temp fix for the Native CoreML scenarios
      std::vector<std::string> mlmodel_files;
      std::copy_if(
          files.begin(), files.end(), std::back_inserter(mlmodel_files),
          [](const std::string& file) {
            return std::filesystem::path(file).extension() == ".mlmodel";
          });
      if (!mlmodel_files.empty()) files = mlmodel_files;
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
        scenario_data.source_to_path_map[task->GetSourcePath()] = file;
        scenario_data.path_to_source_map[file] = task->GetSourcePath();
        paths.push_back(file);
      }
    }
    return true;
  };

  if (!retrievePathsFromTasks(model_files_storage_tasks,
                              scenario_data.model_file_paths))
    return false;
  if (!retrievePathsFromTasks(model_ext_files_storage_tasks,
                              scenario_data.asset_file_paths))
    return false;
  if (!retrievePathsFromTasks(input_storage_tasks,
                              scenario_data.input_file_paths))
    return false;
  if (!retrievePathsFromTasks(assets_storage_tasks,
                              scenario_data.asset_file_paths))
    return false;

  for (const auto& task : other_storage_tasks) {
    if (unpackIfNecessary(task).empty()) return false;
  }

  if (retrieve_results_file && !dependencies_only) {
    if (unpackIfNecessary(other_storage_tasks.front()).empty()) {
      LOG4CXX_ERROR(logger_, "Failed to obtain Results Verification File");
      return false;
    }
    scenario_data.output_results_file_paths =
        other_storage_tasks.front()->GetResPath();
  }

  return true;
}

}  // namespace cil
