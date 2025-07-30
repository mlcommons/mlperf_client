/**
 * @file progressable_task.h
 * 
 * @brief The ProgressableTask class is the base class for all the tasks that can be executed in the TaskScheduler.
*/
#ifndef PROGRESSABLE_TASK_H_
#define PROGRESSABLE_TASK_H_

#include <chrono>
#include <string>

namespace cil {

class ProgressableTask {
  /**
   * @class ProgressableTask
   * @brief The ProgressableTask class is the base class for all the tasks that
   * can be executed in the TaskScheduler.
   */
 public:
  /**
   * @brief Run the task.
   *
   * This method is responsible for running the task and it is called by the
   * TaskScheduler.
   *
   * @return true If the task is successfully run, false otherwise.
   */
  virtual bool Run() = 0;
  /**
   * @brief Cancel the task.
   *
   * This method is responsible for canceling the task and it is called by the
   * TaskScheduler. The task may not be canceled immediately, it is up to the
   * task to check if it is canceled and stop the execution.
   */
  virtual void Cancel() = 0;
  /**
   * @brief Get the reason for skipping the task.
   *
   * This method is responsible for getting the reason for skipping the task.
   *
   * @return std::string The reason for skipping the task.
   */
  virtual std::string getSkippingReason() const { return ""; }
  
  /**
   * @brief Check if the task can be skipped.
   *
   * This method is responsible for checking if the task can be skipped.
   *
   * @return bool True if the task can be skipped, false otherwise.
   */
  virtual bool CheckIfTaskCanBeSkipped() { return false; }
  /**
   * @brief Get the error message.
   *
   * This method is responsible for getting the error message.
   *
   * @return std::string The error message.
   */
  virtual std::string getErrorMessage() const { return ""; }
  /**
   * @brief Allow the task to be called as a function object.
   *
   * This method is responsible for allowing the task to be called as a function
   * object in the TaskScheduler.
   *
   */
  bool operator()() { return Run(); }

  /**
   * @brief Get the progress of the task.
   *
   * This method is responsible for getting the progress of the task.
   *
   * @return int The progress percentage of the task (0-100), -1 if the progress
   * is unknown.
   */
  virtual int GetProgress() const = 0;
  /**
   * @brief Get the total number of steps in the task.
   *
   * This method returns the total number of steps required to complete the
   * task.
   *
   * @return int The total number of steps, or 0 if the total steps are unknown.
   */
  virtual int GetTotalSteps() const { return 0; }
  /**
   * @brief Get the current step of the task.
   *
   * This method returns the number of steps that have been completed so far.
   *
   * @return int The current completed step count, or 0 if the progress is
   * unknown.
   */
  virtual int GetCurrentStep() const { return 0; }
  /**
   * @enum Status
   * @brief The status of the task.
   */
  enum class Status {
    kReady = 0,
    kInitializing,
    kRunning,
    kCompleted,
    kFailed,
    kCanceled,
    KSkipped
  };
  /**
   * @brief Convert the status to a string.
   *
   * This method is responsible for converting the status to a string.
   */
  static std::string StatusToString(Status status) {
    switch (status) {
      case Status::kReady:
        return "Ready";
      case Status::kInitializing:
        return "Initializing";
      case Status::kRunning:
        return "Running";
      case Status::kCompleted:
        return "Completed";
      case Status::kFailed:
        return "Failed";
      case Status::kCanceled:
        return "Canceled";
      case Status::KSkipped:
        return "Skipped";
      default:
        return "Unknown Status";
    }
  }

  /**
   * @brief Get the status of the task.
   *
   * This method is responsible for getting the status of the task.
   */
  virtual Status GetStatus() const = 0;
  /**
   * @brief Get the start time of the task.
   *
   * This method is responsible for getting the start time of the task.
   */
  virtual std::chrono::high_resolution_clock::time_point GetStartTime()
      const = 0;
  /**
   * @brief Get the description of the task.
   *
   * This method is responsible for getting the description of the task.
   */
  virtual std::string GetDescription() { return "ProgressableTask"; };
  virtual ~ProgressableTask(){};
};

}  // namespace cil

#endif  // !PROGRESSABLE_TASK_H_
