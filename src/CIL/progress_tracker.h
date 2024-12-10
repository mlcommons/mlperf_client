#ifndef PROGRESS_TRACKER_H_
#define PROGRESS_TRACKER_H_

#include <chrono>
#include <memory>
#include <vector>

#include "progressable_task.h"

namespace cil {

class ProgressTracker {
 public:
  ProgressTracker(size_t expected_task_count,
                  const std::string& task_description,
                  const std::chrono::milliseconds& update_interval =
                      std::chrono::seconds(1));

  ~ProgressTracker() = default;
  ProgressTracker(const ProgressTracker&) = default;

  void StartTracking();
  void StopTracking();

  void AddTask(std::shared_ptr<ProgressableTask> task);
  void Update();
  void UpdateUntilCompletion();
  bool RequestInterrupt();
  bool Finished() const;
  const std::vector<std::shared_ptr<ProgressableTask>>& GetTasks() const {
    return tasks_;
  }
  void RemoveSkippedTasks() {
    tasks_.erase(
        std::remove_if(tasks_.begin(), tasks_.end(),
                       [](const std::shared_ptr<ProgressableTask>& task) {
                         return task->GetStatus() ==
                                ProgressableTask::Status::KSkipped;
                       }),
        tasks_.end());
  }
  int GetTaskCount() const { return tasks_.size(); }

 private:
  std::string task_description_;
  std::vector<std::shared_ptr<ProgressableTask>> tasks_;
  size_t expected_task_count_;
  size_t current_task_index_;
  std::chrono::milliseconds update_interval_;
  const char rotating_cursor_[4];
  size_t rotating_cursor_position_;
  const std::string description_template_;

  bool interactive_console_mode_;
  int console_width_;

  void DisplayCurrentTaskAndOverallProgress();

  std::string FormatToConsoleWidth(const std::string& stream_string,
                                   const std::string& description) const;
  std::string ReplaceDescriptionSubString(const std::string& stream_string,
                                          const std::string& description) const;
};

}  // namespace cil

#endif  // !PROGRESS_TRACKER_H_
