#include "storage_task.h"

#include <log4cxx/logger.h>

#include "storage.h"

using namespace log4cxx;

LoggerPtr loggerStorageTask(Logger::getLogger("Downloader"));

namespace cil {
StorageTask::StorageTask(std::shared_ptr<Storage> storage,
                         const std::string& file_name,
                         const std::string& sub_dir,
                         bool cache_local_files)
    : storage_(storage),
      file_name_(file_name),
      sub_dir_(sub_dir),
      cache_local_files_(cache_local_files),
      status_(Status::kReady),
      progress_(0) {}

std::string StorageTask::GetSourcePath() const { return file_name_; }

std::string StorageTask::GetResPath(bool absolute) const {
  if (absolute) {
    auto resPath = std::filesystem::path(res_path_);
    auto absolutePath = std::filesystem::absolute(resPath);
    return absolutePath.string().c_str();
  }
  return res_path_;
}

bool StorageTask::CheckIfTaskCanBeSkipped() {
  pre_check_done_ = true;
  bool should_skip = CheckIfFileExistsInStorage(true);
  if (should_skip) {
    status_ = Status::KSkipped;
    progress_ = 100;
  }
  return should_skip;
}

bool StorageTask::CheckIfFileExistsInStorage(bool ignore_cache) {
  std::string file_name = storage_->ExtractFilenameFromURI(file_name_);
  is_target_file_valid_ = !file_name.empty();

  if (!is_target_file_valid_) {
    LOG4CXX_ERROR(loggerStorageTask, "Invalid file URI: " << file_name_);
    error_message_ = "File URI is invalid or empty";
    return false;
  }

  if (storage_->IsForceDownloadEnabled()) {
    res_path_ = "";
    return false;
  }

  // File name is extracted from the URI
  auto res_path =
      storage_->CheckIfLocalFileExists(file_name_, sub_dir_, ignore_cache, cache_local_files_);
  if (!res_path.empty()) {
    LOG4CXX_INFO(
        loggerStorageTask,
        "File: "
            << file_name
            << " was not downloaded, the cached file will be used instead: "
            << res_path);
    res_path_ = res_path;
  }

  return !res_path_.empty();
}

bool StorageTask::Run() {
  if (status_ != Status::kReady) return res_path_.empty();
  start_time_ = std::chrono::high_resolution_clock::now();
  status_ = Status::kInitializing;
  if (!pre_check_done_)
    CheckIfFileExistsInStorage(
        false);  // Check if file exists in storage without ignoring cache
  if (res_path_.empty()) {
    if (!is_target_file_valid_) {
      status_ = Status::kFailed;
    } else {
      status_ = Status::kRunning;
      res_path_ = "";
      if (!storage_->FindFile(
              file_name_, sub_dir_,
              [this](int percent) { progress_ = percent; }, res_path_)) {
        error_message_ = "Failed to get the file: " + file_name_;
      }
      status_ = res_path_.empty() ? Status::kFailed : Status::kCompleted;
    }
  } else {
    status_ = Status::KSkipped;
  }
  progress_ = 100;
  return res_path_.empty();
}

void StorageTask::Cancel() {
  // can not be canceled if already completed
  if (status_ == Status::kCompleted) return;
  status_ = Status::kCanceled;
  storage_->StopDownload();
}

int StorageTask::GetProgress() const { return progress_; }

ProgressableTask::Status StorageTask::GetStatus() const { return status_; }

std::chrono::high_resolution_clock::time_point StorageTask::GetStartTime()
    const {
  return start_time_;
}

std::string StorageTask::GetDescription() {
  std::string description =
      std::string("Download ") +
      std::filesystem::path(file_name_).filename().string();
  return description;
}

}  // namespace cil
