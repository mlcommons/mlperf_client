#ifndef STORAGE_TASK_H_
#define STORAGE_TASK_H_

#include "progressable_task.h"

namespace cil {

class Storage;

class StorageTask : public ProgressableTask {
 public:
  StorageTask(std::shared_ptr<Storage> storage, const std::string& file_name,
              const std::string& sub_dir);
  std::string GetSourcePath() const;
  std::string GetResPath(bool absolute = false) const;

  // ProgressableTask methods
  bool Run() override;
  void Cancel() override;
  int GetProgress() const override;
  Status GetStatus() const override;
  std::chrono::high_resolution_clock::time_point GetStartTime() const override;
  std::string GetDescription() override;
  std::string getSkippingReason() const override {
    return "using cached file, download not needed";
  }
  std::string getErrorMessage() const override { return error_message_; }
  bool CheckIfTaskCanBeSkipped() override;

 private:
  std::shared_ptr<Storage> storage_;
  std::string file_name_;
  std::string sub_dir_;
  std::string res_path_;
  std::string error_message_;
  bool pre_check_done_ = false;
  bool is_target_file_valid_ = false;

  std::atomic<Status> status_;
  std::atomic<int> progress_;
  std::atomic<std::chrono::high_resolution_clock::time_point> start_time_;

  bool CheckIfFileExistsInStorage(bool ignore_cache);
};

}  // namespace cil

#endif  // !STORAGE_TASK_H_
