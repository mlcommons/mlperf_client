#include "download_stage.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>

#include "disk_space_tracker.h"
#include "execution_provider_config.h"
#include "llm/portable_python.h"
#include "model_config.h"
#include "runner.h"
#include "storage.h"
#include "storage_task.h"
#include "task_scheduler.h"
#include "url_cache_manager.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {
namespace {

template <typename Container>
std::unordered_map<std::string, std::string> ContainerToMap(
    const Container& input) {
  std::unordered_map<std::string, std::string> output;
  for (const auto& key : input) output[key] = "";
  return output;
}

template <typename Container>
std::vector<std::pair<std::string, std::string>> ContainerToVector(
    const Container& input) {
  std::vector<std::pair<std::string, std::string>> output;
  for (const auto& key : input) output.emplace_back(key, "");
  return output;
}

}  // namespace

const std::chrono::milliseconds DownloadStage::kProgressInterval =
    std::chrono::milliseconds(200);

bool DownloadStage::Run(const ScenarioConfig& scenario_config,
                        ScenarioData& scenario_data,
                        const ReportProgressCb& raport_progress_cb) {
  const auto download_behavior =
      config_.GetSystemConfig().GetDownloadBehavior();

  // Skip the disk check for non-downloading, interactive, or size-only runs;
  // the sizing sweep would otherwise re-trigger prompt's std::cin per file.
  const bool skip_space_check =
      download_behavior == "skip_all" || download_behavior == "prompt" ||
      download_behavior == "deps_only_enumeration" ||
      download_behavior == "collect_file_sizes_only" ||
      download_behavior == "collect_file_sizes_deps_only";
  if (!skip_space_check &&
      !CheckDiskSpace(scenario_config, scenario_data, raport_progress_cb))
    return false;

  return RunStorageTasks(scenario_config, scenario_data, raport_progress_cb,
                         /*collect_sizes_only=*/false,
                         /*include_local_sizes=*/false);
}

bool DownloadStage::CheckDiskSpace(const ScenarioConfig& scenario_config,
                                   ScenarioData& scenario_data,
                                   const ReportProgressCb& raport_progress_cb) {
  LOG4CXX_INFO(logger_, "\nChecking disk space before downloading...");

  // Sizing sweep: HEAD every remote file and stat every local file that would
  // be copied, recording sizes into scenario_data.file_sizes.
  if (!RunStorageTasks(scenario_config, scenario_data, raport_progress_cb,
                       /*collect_sizes_only=*/true,
                       /*include_local_sizes=*/true)) {
    LOG4CXX_ERROR(logger_,
                  "Disk space check: failed to evaluate file sizes.\n");
    const std::string reason =
        last_storage_error_.empty()
            ? "the size of one or more required files could not be determined"
            : last_storage_error_;
    SetErrorMessage(
        "Could not complete the disk-space check: " + reason +
        ".\nIf all required files are already downloaded, run with -b skip_all "
        "to skip this check and run offline.\n");
    return false;
  }

  DiskSpaceTracker tracker;
  for (const auto& [url, info] : scenario_data.file_sizes)
    tracker.RegisterPlannedDownload(info.planned_path, info.size);

  std::string failure_message;
  const bool enough = tracker.HasEnoughSpace(logger_, "Disk space check failed",
                                             &failure_message);
  if (enough) {
    LOG4CXX_INFO(logger_, "Disk space check passed.\n");
  } else {
    // The failure callback only sees the stage name ("Download"), so spell out
    // the disk-space cause here and surface it for the GUI/CLI status.
    LOG4CXX_ERROR(logger_,
                  "Not enough disk space for the required downloads.\n");
    SetErrorMessage("Not enough disk space to download benchmark assets.\n" +
                    failure_message);
  }
  return enough;
}

bool DownloadStage::RunStorageTasks(const ScenarioConfig& scenario_config,
                                    ScenarioData& scenario_data,
                                    const ReportProgressCb& raport_progress_cb,
                                    bool collect_sizes_only,
                                    bool include_local_sizes) {
  last_storage_error_.clear();
  auto download_behavior = config_.GetSystemConfig().GetDownloadBehavior();
  auto cache_local_files = config_.GetSystemConfig().GetCacheLocalFiles();

  bool dependencies_only = download_behavior == "deps_only" ||
                           download_behavior == "deps_only_enumeration" ||
                           download_behavior == "collect_file_sizes_deps_only";
  // A sizes-only sweep is driven either by the caller (the pre-flight disk
  // space check), by a standalone "collect_file_sizes_only" run, or by the
  // combined deps-only sizing pre-pass used to size enumeration downloads.
  const bool get_sizes_only =
      collect_sizes_only || download_behavior == "collect_file_sizes_only" ||
      download_behavior == "collect_file_sizes_deps_only";

  bool retrieve_results_file = !scenario_config.GetResultsFile().empty();
  if (retrieve_results_file) {
    if (utils::FileExists(scenario_config.GetResultsFile())) {
      // if the file is already downloaded, no need to download it again
      scenario_data.output_results_file_paths =
          scenario_config.GetResultsFile();
      retrieve_results_file = false;
      if (!get_sizes_only) {
        LOG4CXX_INFO(logger_, "Results Verification File "
                                  << scenario_config.GetResultsFile()
                                  << " is already downloaded");
      }
    }
  } else if (!get_sizes_only) {
    LOG4CXX_INFO(
        logger_,
        "Results Verification File is not set, the default file will be used");
  }

  ProgressTracker storage_tasks_progress_tracker(
      0, "download", kProgressInterval, get_sizes_only);

  TaskScheduler storage_tasks_scheduler("Download Scheduler");

  using StoragePtr = std::shared_ptr<Storage>;
  using StorageTaskPtr = std::shared_ptr<StorageTask>;
  using StorageTaskVector = std::vector<StorageTaskPtr>;
  using StorageVector = std::vector<StoragePtr>;

  std::hash<std::string> hasher;

  const bool force_download = download_behavior == "forced";

  const auto path_to_subdir = BuildPathToSubdirMap(scenario_config);

  // When all models share the same base, scenario-level files (inputs, assets,
  // results-verification) live under that base. With mixed bases there is no
  // single owner, so they fall back to the scenario root.
  const auto& models = scenario_config.GetModels();
  const std::string& first_base = models.front().GetModelBaseName();
  const bool all_same_base = std::all_of(
      models.begin(), models.end(),
      [&](const ModelConfig& m) { return m.GetModelBaseName() == first_base; });
  const std::string scenario_files_subdir = ComputeModelDir(
      scenario_config, all_same_base ? first_base : std::string{});

  auto cancelStorageTasks = [&]() {
    storage_tasks_scheduler.CancelTasks();
    for (const auto& task : storage_tasks_progress_tracker.GetTasks())
      task->Cancel();
  };

  DiskSpaceTracker space_tracker;
  if (!get_sizes_only) {
    for (const auto& [url, info] : scenario_data.file_sizes)
      space_tracker.RegisterPlannedDownload(info.planned_path, info.size);
  }

  auto addStorageTasks = [&](StoragePtr storage, const auto& paths,
                             StorageTaskVector& tasks,
                             const std::string& sub_dir,
                             bool use_hash = false) {
    for (const auto& [file_path, file_key] : paths) {
      // Skip task creation when the plan cache already says this URL is
      // locally cached. .zip files keep their task so UnpackFileIfNecessary
      // later in this function still runs (idempotent if already extracted).
      if (const auto ext =
              std::filesystem::path(file_path).extension().string();
          ext != ".zip" && ext != ".ZIP" && !get_sizes_only &&
          URLCacheManager::GetCachedSize(file_path).value_or(UINT64_MAX) == 0) {
        continue;
      }

      std::string final_dir = sub_dir;
      if (auto it = path_to_subdir.find(file_path); it != path_to_subdir.end())
        final_dir = it->second;
      if (use_hash) final_dir += "/" + std::to_string(hasher(file_key));
      auto storage_task = std::make_shared<StorageTask>(
          storage, file_path, final_dir, cache_local_files);
      tasks.push_back(storage_task);
      std::string task_name = "Download " + file_path;
      storage_tasks_scheduler.ScheduleTask(
          task_name, [this, storage_task, &cancelStorageTasks, &space_tracker,
                      get_sizes_only, logger = logger_]() {
            (*storage_task)();
            const auto status = storage_task->GetStatus();
            if (status == ProgressableTask::Status::kFailed) {
              cancelStorageTasks();
              return;
            }
            // A sizes-only sweep writes nothing to disk, so there is no
            // mid-download space check to run.
            if (!get_sizes_only &&
                status == ProgressableTask::Status::kCompleted) {
              space_tracker.MarkCompleted(storage_task->GetResPath());
              std::string failure_message;
              if (!space_tracker.HasEnoughSpace(
                      logger, "Disk space exhausted mid-download",
                      &failure_message)) {
                SetErrorMessage("Ran out of disk space during download.\n" +
                                failure_message);
                cancelStorageTasks();
              }
            }
          });
      storage_tasks_progress_tracker.AddTask(storage_task);
    }
  };

  auto url_cache_manager =
      std::make_shared<URLCacheManager>(unpacker_.GetDepsDir());

  auto data_storage =
      std::make_shared<Storage>(data_dir_, url_cache_manager, force_download,
                                false, get_sizes_only, include_local_sizes);

  StorageTaskVector model_files_storage_tasks;
  StorageTaskVector model_ext_files_storage_tasks;
  StorageTaskVector input_storage_tasks;
  StorageTaskVector assets_storage_tasks;
  StorageTaskVector other_storage_tasks;
  StorageTaskVector ep_dependencies_storage_tasks;

  if (!dependencies_only) {
    addStorageTasks(data_storage, scenario_config.GetModelFiles(),
                    model_files_storage_tasks, scenario_files_subdir, true);
    addStorageTasks(data_storage, scenario_config.GetModelExtraFiles(),
                    model_ext_files_storage_tasks, scenario_files_subdir, true);
    addStorageTasks(data_storage,
                    ContainerToVector(scenario_config.GetInputs()),
                    input_storage_tasks, scenario_files_subdir);
    addStorageTasks(data_storage, ContainerToMap(scenario_config.GetAssets()),
                    assets_storage_tasks, scenario_files_subdir);
  }

  StorageVector ep_dependencies_storages;
  // loop over the execution providers and download the dependencies
  for (const auto& ep : scenario_config.GetExecutionProviders()) {
    if (!BenchmarkRunner::IsEpSupportedOnThisPlatform(scenario_config.GetName(),
                                                      ep.GetFullName()))
      continue;

    const auto& ep_external_dependencies = ep.GetDependencies();
    if (ep_external_dependencies.empty()) continue;

    // create path variable to store the destination directory
    fs::path dest_dir =
        GetExecutionProviderParentLocation(ep, unpacker_.GetDepsDir());

    ep_dependencies_storages.push_back(std::make_shared<Storage>(
        dest_dir.string(), url_cache_manager, force_download, false,
        get_sizes_only, include_local_sizes));
    addStorageTasks(ep_dependencies_storages.back(),
                    ContainerToMap(ep_external_dependencies),
                    ep_dependencies_storage_tasks, "");
  }

  if (retrieve_results_file && !dependencies_only)
    addStorageTasks(data_storage,
                    std::map<std::string, std::string>{
                        {scenario_config.GetResultsFile(), ""}},
                    other_storage_tasks, scenario_files_subdir);

  // A non-empty PythonPath (config, --python-path dir, or "system") means the
  // bundled Python is not used, so don't download it.
  if (!dependencies_only && scenario_config.IsAgentic() &&
      config_.GetSystemConfig().GetPythonPath().empty()) {
    if (std::string python_url = GetPortablePythonAssetUrl();
        !python_url.empty())
      addStorageTasks(data_storage,
                      std::map<std::string, std::string>{{python_url, ""}},
                      other_storage_tasks, GetPortablePythonDirName());
  }

  // Internal EPs dependencies
  const auto& ep_storage_files = ep_dependencies_manager_.GetEpsStorageFiles();
  for (const auto& [ep_deps_dir, files] : ep_storage_files) {
    ep_dependencies_storages.push_back(std::make_shared<Storage>(
        ep_deps_dir, url_cache_manager, force_download, true, get_sizes_only,
        include_local_sizes));
    addStorageTasks(ep_dependencies_storages.back(), ContainerToMap(files),
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
    if (!get_sizes_only)
      LOG4CXX_INFO(logger_, "\nDownloading necessary files...\n");

    if (!raport_progress_cb(storage_tasks_progress_tracker,
                            storage_tasks_scheduler)) {
      cancelStorageTasks();
      storage_tasks_scheduler.Join();
      LOG4CXX_INFO(logger_, "\nModel "
                                << scenario_config.GetDisplayName()
                                << ", Download Stage interrupted, stopping...");
      return false;
    }
  }

  for (const auto& task : storage_tasks_progress_tracker.GetTasks()) {
    if (task->GetStatus() == ProgressableTask::Status::kFailed) {
      if (last_storage_error_.empty())
        last_storage_error_ = task->getErrorMessage();
      std::string error_string = "Download Stage failed, stopping.";
      if (auto log_path = utils::GetErrorLogFilePath(); !log_path.empty())
        error_string += " Check the log file for details: " + log_path;
      LOG4CXX_ERROR(logger_, error_string);
      return false;
    }
  }

  if (get_sizes_only) {
    scenario_data.file_sizes = data_storage->GetFileSizes();
    for (const auto& storage : ep_dependencies_storages)
      scenario_data.file_sizes.insert(storage->GetFileSizes().begin(),
                                      storage->GetFileSizes().end());
    return true;
  }

  unpacking_files_logged_ = false;

  auto retrievePathsFromTasks = [&](const StorageTaskVector& tasks,
                                    std::vector<std::string>& paths,
                                    std::string_view keep_extension =
                                        std::string_view{}) {
    for (const auto& task : tasks) {
      auto files = UnpackFileIfNecessary(task->GetResPath(), keep_extension);
      if (files.empty()) return false;

      for (auto& file : files) {
        std::string path_str = file.string();
        file = fs::proximate(utils::NormalizePath(path_str));
        scenario_data.source_to_path_map[task->GetSourcePath()] = path_str;
        scenario_data.path_to_source_map[path_str] = task->GetSourcePath();
        paths.push_back(path_str);
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
                              scenario_data.input_file_paths, ".json"))
    return false;
  if (!retrievePathsFromTasks(assets_storage_tasks,
                              scenario_data.asset_file_paths))
    return false;

  for (const auto& task : other_storage_tasks) {
    if (UnpackFileIfNecessary(task->GetResPath()).empty()) return false;
  }

  if (retrieve_results_file && !dependencies_only) {
    if (UnpackFileIfNecessary(other_storage_tasks.front()->GetResPath())
            .empty()) {
      LOG4CXX_ERROR(logger_, "Failed to obtain Results Verification File");
      return false;
    }
    scenario_data.output_results_file_paths =
        other_storage_tasks.front()->GetResPath();
  }

  return true;
}

void DownloadStage::ClearCache(std::string deps_dir) {
  auto url_cache_manager = std::make_shared<URLCacheManager>(deps_dir);
  url_cache_manager->ClearCache();
}

std::string DownloadStage::ComputeModelDir(
    const ScenarioConfig& scenario_config, const std::string& model_base_name) {
  const std::string& name = scenario_config.GetName();
  const std::string& display = scenario_config.GetDisplayName();
  if (name != "txt2txt") return display;
  return model_base_name.empty() ? name : name + "/" + model_base_name;
}

std::unordered_map<std::string, std::string>
DownloadStage::BuildPathToSubdirMap(
    const ScenarioConfig& scenario_config) const {
  std::unordered_map<std::string, std::string> path_to_subdir;
  auto record_model_paths = [&](const ModelConfig& m) {
    const std::string sub =
        ComputeModelDir(scenario_config, m.GetModelBaseName());
    for (const auto& p :
         {m.GetFilePath(), m.GetDataFilePath(), m.GetTokenizerPath()}) {
      if (!p.empty()) path_to_subdir[p] = sub;
    }
    for (const auto& p : m.GetAdditionalPaths()) path_to_subdir[p] = sub;
  };
  for (const auto& m : scenario_config.GetModels()) record_model_paths(m);
  for (const auto& ep : scenario_config.GetExecutionProviders())
    for (const auto& m : ep.GetModels()) record_model_paths(m);

  for (const auto& [url, subdir] : ep_dependencies_manager_.GetFileSubdirs())
    path_to_subdir[url] = subdir;
  return path_to_subdir;
}

std::vector<std::filesystem::path> DownloadStage::UnpackFileIfNecessary(
    const std::filesystem::path& file_path, std::string_view keep_extension) {
  if (!fs::exists(file_path)) return {};

  if (file_path.extension() == ".zip" || file_path.extension() == ".ZIP") {
    if (!unpacking_files_logged_) {
      LOG4CXX_INFO(logger_, "Unpacking downloaded files...");
      unpacking_files_logged_ = true;
    }

    auto unpacked = Unpacker::UnpackFilesFromZIP(
        file_path.string(), file_path.parent_path().string());

    if (unpacked.empty()) return {};

    std::vector<fs::path> files;
    files.reserve(unpacked.size());
    for (auto& path_str : unpacked) files.emplace_back(std::move(path_str));

    // Only return files matching keep_extension (case-insensitive) when set;
    // other extracted files remain on disk but are not registered.
    if (!keep_extension.empty()) {
      std::erase_if(files, [&keep_extension](const fs::path& f) {
        std::string ext = f.extension().string();
        std::ranges::transform(ext, ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });
        return ext != keep_extension;
      });
    }

    return files;
  }

  return {file_path};
}

}  // namespace cil
