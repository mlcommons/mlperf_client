#ifndef GUI_PROGRESS_TRACKER_HANDLER_H_
#define GUI_PROGRESS_TRACKER_HANDLER_H_

#include <QObject>

#include "progress_tracker.h"
#include "progress_tracker_handler.h"

namespace gui {

/**
 * @class GuiProgressTrackerHandler
 * @brief Connects progress tracking logic to the GUI.
 */
class GuiProgressTrackerHandler : public QObject,
                                  public cil::ProgressTrackerHandler {
  Q_OBJECT
 public:
  /**
   * @brief Construct handler with interrupt flag reference.
   * @param interrupt Reference interrupt flag.
   */
  explicit GuiProgressTrackerHandler(std::atomic<bool>& interrupt);

  /**
   * @brief Start tracking a new operation.
   * @param tracker Reference to the progress tracker to start.
   */
  void StartTracking(cil::ProgressTracker& tracker) override;

  /**
   * @brief Update progress for the current operation.
   * @param tracker Reference to the progress tracker to update.
   */
  void Update(cil::ProgressTracker& tracker) override;

  /**
   * @brief Request interruption of the current operation.
   * @param tracker Reference to the progress tracker to interrupt.
   * @return True if interruption was requested successfully.
   */
  bool RequestInterrupt(cil::ProgressTracker& tracker) override;

  /**
   * @brief Set interrupt flag.
   * @param interrupt Boolean indicating whether to interrupt.
   */
  void SetInterrupt(bool interrupt);

 signals:
  /**
   * @brief Notify UI of current task info change.
   * @param name Name of the current task.
   * @param progress Current progress value.
   * @param expected_task_count_ Expected total number of tasks.
   * @param current_task_index_ Index of the current task.
   * @param total_steps Total number of steps.
   * @param current_step Current step index.
   */
  void CurrentTaskInfoChanged(const QString& name, int progress,
                              int expected_task_count_, int current_task_index_,
                              int total_steps, int current_step);

  /**
   * @brief Log message for UI.
   * @param message The log message to display.
   * @param move_start Boolean indicating whether to move to start.
   * @param move_up Boolean indicating whether to move up.
   * @param move_down Boolean indicating whether to move down.
   */
  void LogMessage(const QString& message, bool move_start, bool move_up,
                  bool move_down);

 private:
  std::string current_task_description_;
  size_t current_progress_;
  int expected_task_count_;
  int current_task_index_;
  int total_steps_;
  int current_step_;
};

}  // namespace gui

#endif  // !GUI_PROGRESS_TRACKER_HANDLER_H_
