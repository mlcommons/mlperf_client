#ifndef GUI_PROGRESS_TRACKER_HANDLER_H_
#define GUI_PROGRESS_TRACKER_HANDLER_H_

#include <QObject>

#include "progress_tracker.h"
#include "progress_tracker_handler.h"

namespace gui {

class GuiProgressTrackerHandler : public QObject,
                                  public cil::ProgressTrackerHandler {
  Q_OBJECT
 public:
  explicit GuiProgressTrackerHandler(std::atomic<bool>& interrupt);

  void StartTracking(cil::ProgressTracker& tracker) override;

  void Update(cil::ProgressTracker& tracker) override;
  bool RequestInterrupt(cil::ProgressTracker& tracker) override;

  void SetInterrupt(bool interrupt);

 signals:
  void CurrentTaskInfoChanged(const QString& name, int progress,
                              int expected_task_count_, int current_task_index_,
                              int total_steps, int current_step);
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
