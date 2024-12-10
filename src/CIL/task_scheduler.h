/**
 * @file task_scheduler.h
 * @brief TaskScheduler class for scheduling and executing tasks sequentially.
 * The tasks are executed asynchronously in a separate thread and there's an
 * option to wait for the completion of all tasks, and the tasks can be canceled
 * at any time.
 */
#ifndef TASKSCHEDULER_H_
#define TASKSCHEDULER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace cil {

class ProgressableTask;
/**
 * @class TaskScheduler
 * @brief TaskScheduler class for scheduling and executing tasks sequentially.
 * The TaskScheduler allows tasks to be executed sequentially in a separate
 * thread, the Tasks can be added with or without a name for debugging
 * purposes.The execution can be started asynchronously and then joined while
 * waiting for completion.
 */
class TaskScheduler {
 public:
  /**
   * @brief Construct a new Task Scheduler object.
   *
   * @param scheduler_name Name of the scheduler  (useful for debugging).
   */
  TaskScheduler(const std::string& scheduler_name = "");
  ~TaskScheduler();

  TaskScheduler(const TaskScheduler&);
  TaskScheduler(TaskScheduler&&);

  /**
   * @brief Adds a task to the scheduler.
   *
   * @param task A pointer to the function to be executed.
   */
  void ScheduleTask(std::function<void()> task);

  /**
   * @brief Add a task to the scheduler.
   *
   * @param task_name Name of the task (useful for debugging).
   * @param task A pointer to the function to be executed.
   */
  void ScheduleTask(const std::string& task_name, std::function<void()> task);

  /**
   * @brief Execute the tasks asynchronously.
   *
   * This method will start a new thread and execute the tasks in the queue one
   * by one.
   */
  void ExecuteTasksAsync();
  /**
   * @brief Cancel all the tasks in the queue.
   *
   * This method will cancel all the tasks in the queue and notify the
   * completion. The tasks that are already running will not be canceled
   * immediately.
   */
  void CancelTasks();
  /**
   * @brief Wait for the completion of the thread executing the tasks.
   *
   */
  void Join();
  /**
   * @brief Check if all the tasks are copmleted.
   *
   * @return True if all the tasks are completed or canceled, false otherwise.
   */
  bool AreTasksCompleted() const;

  /**
   * @brief Wait for the completion of the tasks with a timeout.
   *
   * @param timeout_msec Timeout in milliseconds.
   * @return True if all the tasks are completed or canceled, false otherwise.
   */

  bool WaitForCompletionWithTimeout(unsigned int timeout_msec);

  /**
   * @brief Set the name of the scheduler.
   *
   * @param name Name of the scheduler (useful for debugging).
   */
  void SetSchedulerName(const std::string& name) { scheduler_name_ = name; }

 private:
  void ExecuteTasks();

  std::unique_ptr<std::thread> task_thread_;
  std::queue<std::pair<std::string, std::function<void()>>> tasks_;
  std::mutex queue_mutex_;
  std::atomic<bool> tasks_completed_;
  std::atomic<bool> tasks_canceled_;
  std::mutex completion_mutex_;
  std::condition_variable completion_condition_;
  std::string scheduler_name_;
};
}  // namespace cil

#endif  // !TASKSCHEDULER_H_
