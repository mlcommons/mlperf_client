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

  std::chrono::milliseconds GetUpdateInterval() const {
    return update_interval_;
  }

  const std::string& GetTaskDescription() const { return task_description_; }

  size_t GetExpectedTaskCount() const { return expected_task_count_; }

  class HandlerBase {
   public:
    HandlerBase() = default;

    virtual ~HandlerBase() = default;
    HandlerBase(const HandlerBase&) = delete;

    HandlerBase& operator=(const HandlerBase&) = delete;

    virtual void StartTracking(ProgressTracker& tracker) {
      tracker.StartTracking();
    }
    virtual void StopTracking(ProgressTracker& tracker) {
      tracker.StopTracking();
    }

    virtual void Update(ProgressTracker& tracker) = 0;
    virtual bool RequestInterrupt(ProgressTracker& tracker) = 0;

    virtual std::atomic<bool>& GetInterrupt() = 0;

    size_t& GetCurrentTaskIndex(ProgressTracker& tracker) {
      return tracker.current_task_index_;
    }
  };

 private:
  std::string task_description_;
  std::vector<std::shared_ptr<ProgressableTask>> tasks_;
  size_t expected_task_count_;
  size_t current_task_index_;
  std::chrono::milliseconds update_interval_;

  friend class HandlerBase;
};

}  // namespace cil

#endif  // !PROGRESS_TRACKER_H_
