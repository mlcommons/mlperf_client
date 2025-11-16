#ifndef __REALTIME_PAGE_CONTROLLER_H__
#define __REALTIME_PAGE_CONTROLLER_H__

#include <QObject>

#include "controllers/abstract_controller.h"

class QThread;
class QTimer;
class ExecutionProgressWidget;

namespace gui {

class GuiProgressTrackerHandler;
struct EPInformationCard;
struct BenchmarkStatus;

namespace views {
class RealTimeMonitoringPage;
}
namespace controllers {
/**
 * @class RealtimePageController
 * @brief Manages logic and state for real-time monitoring and benchmarking
 * page.
 */
class RealtimePageController : public AbstractController {
  Q_OBJECT
 public:
  explicit RealtimePageController(std::atomic<bool>& interrupt,
                                  QObject* parent = nullptr);
  virtual ~RealtimePageController();

  void SetView(views::RealTimeMonitoringPage* view);

  void InitExecutionWithEPs(const QList<EPInformationCard>& eps);

  /**
   * @brief Get the display name of an execution provider.
   * @param index Zero-based index of the execution provider.
   * @return Display name of the specified execution provider.
   */
  QString GetEPDisplayName(int index) const;

  /**
   * @brief Request confirmation to download data.
   * @param size_in_bytes size of the data to be downloaded, in bytes.
   * @return true if the user confirms the download, false otherwise.
   */
  bool RequestDownload(uint64_t size_in_bytes);

  /**
   * @brief Show benchmark status.
   * @param action_type string representing the benchmark action.
   * @param status data representing the benchmark action status.
   */
  void ShowStatus(const QString& action_type, const BenchmarkStatus& status);

 public slots:
  /**
   * @brief Select an execution provider by index.
   * @param index Zero-based index of the execution provider to select.
   */
  void SetEpSelected(int index);

  /**
   * @brief Moves to the next EP, passing success status of current.
   * @param current_success Boolean indicating whether the current execution
   * provider completed successfully.
   */
  void MoveToNextEP(bool current_success);

  /**
   * @brief Set progress ratios for execution stages.
   * @param ratios Vector of progress ratios for each stage.
   */
  void SetStagesProgressRatios(const QVector<double>& ratios);

  /**
   * @brief Update the current execution stage.
   * @param current_stage Zero-based index of the current execution stage.
   */
  void SetCurrentStage(int current_stage);
  void AppendLogMessage(const QString& message);
  void SetCurrentTaskName(const QString& name);
  void EnableLogs(bool enable);

  /**
   * @brief Access the progress tracker handler.
   * @return Pointer to the GuiProgressTrackerHandler instance.
   */
  GuiProgressTrackerHandler* GetProgressHandler() const;

 signals:
  void ExecutionCancelRequested();

 private slots:
  /**
   * @brief Toggle visibility of counters in the UI.
   * @param checked Boolean indicating whether counters should be hidden.
   */
  void OnHideCountersToggled(bool checked);

  /**
   * @brief Respond to system monitoring widget visibility changes.
   * @param visible Boolean indicating whether the monitoring widget is visible.
   */
  void OnSystemMonitoringWidgetVisibilityChanged(bool visible);

  /**
   * @brief Handle periodic monitoring updates.
   */
  void OnMonitoringTimerTimeout();

  /**
   * @brief Respond to user cancel action.
   */
  void OnCancelClicked();

  /**
   * @brief Update UI with current task info.
   * @param name Name of the current task.
   * @param progress Current progress value.
   * @param total_tasks Total number of tasks.
   * @param current_task Current task index.
   * @param total_steps Total number of steps.
   * @param current_step Current step index.
   */
  void OnCurrentTaskInfoChanged(const QString& name, int progress,
                                int total_tasks, int current_task,
                                int total_steps, int current_step);

 private:
  QThread* monitoring_thread_;
  std::atomic_bool monitoring_update_in_progress_;
  QTimer* monitoring_timer_;
  QMap<QString, int> monitoring_widget_indices_;
  QMap<QString, bool> is_gpu_dedicated;

  GuiProgressTrackerHandler* progress_handler_;
  QVector<double> stages_progress_ratios_;
  int current_stage_;

  bool logs_enabled_;

  /**
   * @brief Gets the associated RealTimeMonitoringPage view.
   * @return Pointer to the RealTimeMonitoringPage view.
   */
  views::RealTimeMonitoringPage* GetView() const;

  /**
   * @brief Gets the execution progress widget.
   * @return Pointer to the ExecutionProgressWidget.
   */
  ExecutionProgressWidget* GetExecutionProgressWidget() const;

  void StartMonitoring();

  void StopMonitoring();

  void InitializeMonitoringWidgets();

  void InitializeMonitoringThread();
};
}  // namespace controllers
}  // namespace gui

#endif  // __REALTIME_PAGE_CONTROLLER_H__