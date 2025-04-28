#include "progress_tracker.h"

namespace cil {

ProgressTracker::ProgressTracker(
    size_t expected_task_count, const std::string &task_description,
    const std::chrono::milliseconds &update_interval)
    : task_description_(task_description),
      expected_task_count_(expected_task_count),
      current_task_index_(0),
      update_interval_(update_interval) {}

void ProgressTracker::StartTracking() {
  if (expected_task_count_ == 0) {
    expected_task_count_ = tasks_.size();
  }
}

void ProgressTracker::StopTracking() { expected_task_count_ = 0; }

void ProgressTracker::AddTask(std::shared_ptr<ProgressableTask> task) {
  tasks_.push_back(task);
}

bool ProgressTracker::Finished() const {
  return current_task_index_ == tasks_.size();
}

}  // namespace cil
