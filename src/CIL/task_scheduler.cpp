#include "task_scheduler.h"

#include <log4cxx/logger.h>

using namespace log4cxx;
LoggerPtr loggerTaskScheduler(Logger::getLogger("TaskScheduler"));

namespace cil {
TaskScheduler::TaskScheduler(const std::string& scheduler_name)
    : task_thread_(nullptr),
      tasks_completed_(true),
      tasks_canceled_(false),
      scheduler_name_(scheduler_name) {}

TaskScheduler::TaskScheduler(const TaskScheduler& other)
    : tasks_(other.tasks_), scheduler_name_ ( other.scheduler_name_) {
 
}
TaskScheduler::TaskScheduler(TaskScheduler&& other) {
  task_thread_ = std::move(other.task_thread_);
  tasks_ = std::move(other.tasks_);
  tasks_completed_ = other.tasks_completed_.load();
  tasks_canceled_ = other.tasks_canceled_.load();
  scheduler_name_ = std::move(other.scheduler_name_);
}

TaskScheduler::~TaskScheduler() { Join(); }

void TaskScheduler::ScheduleTask(std::function<void()> task) {
  ScheduleTask("", task);
}

void TaskScheduler::ScheduleTask(const std::string& task_name,
                                 std::function<void()> task) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  tasks_.push(std::make_pair(task_name, task));
  tasks_completed_ = false;
  tasks_canceled_ = false;
}

void TaskScheduler::ExecuteTasksAsync() {
  Join();
  task_thread_ =
      std::make_unique<std::thread>(&TaskScheduler::ExecuteTasks, this);
}

void TaskScheduler::CancelTasks() { tasks_canceled_ = true; }

void TaskScheduler::Join() {
  if (task_thread_ && task_thread_->joinable()) {
    task_thread_->join();
  }
}

bool TaskScheduler::AreTasksCompleted() const { return tasks_completed_; }

bool TaskScheduler::WaitForCompletionWithTimeout(unsigned int timeout_msec) {
  std::unique_lock<std::mutex> lock(completion_mutex_);
  return completion_condition_.wait_for(lock,
                                        std::chrono::milliseconds(timeout_msec),
                                        [this] { return AreTasksCompleted(); });
}

void TaskScheduler::ExecuteTasks() {
  while (true) {
    std::pair<std::string, std::function<void()>> task;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (tasks_.empty() || tasks_canceled_) {
        std::lock_guard<std::mutex> completion_lock(completion_mutex_);
        tasks_completed_ = true;
        completion_condition_.notify_one();
        if (tasks_canceled_) {
          LOG4CXX_DEBUG(loggerTaskScheduler,
                        scheduler_name_ << " tasks execution canceled.");
        } else {
          LOG4CXX_DEBUG(loggerTaskScheduler,
                        scheduler_name_ << " all tasks completed.");
        }

        break;
      }
      task = tasks_.front();
      tasks_.pop();
    }
    try {
      LOG4CXX_DEBUG(loggerTaskScheduler,
                    scheduler_name_ << " executing task: " << task.first);
      task.second();
      LOG4CXX_DEBUG(loggerTaskScheduler, scheduler_name_
                                             << " task execution completed: "
                                             << task.first);
    } catch (const std::exception& e) {
      LOG4CXX_ERROR(loggerTaskScheduler,
                    scheduler_name_
                        << " task " << task.first
                        << " execution failed with exception: " << e.what());
    } catch (...) {
      LOG4CXX_ERROR(loggerTaskScheduler,
                    scheduler_name_
                        << " task " << task.first
                        << " execution failed with unknown exception.");
    }
  }
}

}  // namespace cil