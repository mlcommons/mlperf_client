#include "gui_progress_tracker_handler.h"

#include "../CIL/mlperf_console_appender.h"

namespace gui {

GuiProgressTrackerHandler::GuiProgressTrackerHandler(
    std::atomic<bool> &interrupt)
    : cil::ProgressTrackerHandler(interrupt),
      current_progress_(0),
      current_task_index_(0),
      expected_task_count_(0),
      total_steps_(0),
      current_step_(0) {
  logger_callback_ = [this](const std::string &output, bool move_start,
                            bool move_up, bool move_down) {
    emit LogMessage(QString::fromStdString(output), move_start, move_up,
                    move_down);
  };
}

void GuiProgressTrackerHandler::StartTracking(cil::ProgressTracker &tracker) {
  tracker.StartTracking();

  console_width_ = 120;
  interactive_console_mode_ = true;

  MLPerfConsoleAppender::accessMutex().lock();
}

void GuiProgressTrackerHandler::Update(cil::ProgressTracker &tracker) {
  const auto &task_description = tracker.GetTaskDescription();
  const auto expected_task_count = tracker.GetExpectedTaskCount();
  auto current_task_index = GetCurrentTaskIndex(tracker);
  const auto &current_task = tracker.GetTasks()[current_task_index];

  cil::ProgressableTask::Status task_status = current_task->GetStatus();
  bool task_finished = task_status > cil::ProgressableTask::Status::kRunning;

  if (task_finished) ++current_task_index;

  size_t overall_progress = 0;

  size_t completed_tasks = current_task_index;
  overall_progress = (completed_tasks * 100) / expected_task_count;
  if (!task_finished && completed_tasks != expected_task_count)
    overall_progress += current_task->GetProgress() / expected_task_count;

  int total_steps = 0, current_step = 0;
  if (task_description == "benchmark") {
    total_steps = current_task->GetTotalSteps();
    current_step = current_task->GetCurrentStep();
  }

  if (current_progress_ != overall_progress ||
      current_task_description_ != task_description ||
      expected_task_count_ != expected_task_count ||
      current_task_index_ != current_task_index ||
      total_steps_ != total_steps || current_step_ != current_step) {
    current_progress_ = overall_progress;
    current_task_description_ = task_description;
    expected_task_count_ = expected_task_count;
    current_task_index_ = current_task_index;
    total_steps_ = total_steps;
    current_step_ = current_step;
    emit CurrentTaskInfoChanged(
        QString::fromStdString(current_task_description_), current_progress_,
        expected_task_count_, current_task_index_, total_steps_, current_step_);
  }

  cil::ProgressTrackerHandler::Update(tracker);
}

bool GuiProgressTrackerHandler::RequestInterrupt(
    cil::ProgressTracker &tracker) {
  return interrupt_;
}

void GuiProgressTrackerHandler::SetInterrupt(bool interrupt) {
  interrupt_ = interrupt;
}

}  // namespace gui
