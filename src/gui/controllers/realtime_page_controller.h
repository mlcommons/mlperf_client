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

namespace views {
class RealTimeMonitoringPage;
}
namespace controllers {
class RealtimePageController : public AbstractController {
  Q_OBJECT
 public:
  explicit RealtimePageController(std::atomic<bool>& interrupt,
                                  QObject* parent = nullptr);
  virtual ~RealtimePageController();
  void SetView(views::RealTimeMonitoringPage* view);
  void InitExecutionWithEPs(const QList<EPInformationCard>& eps);

 public slots:
  void SetEpSelected(int index);
  void MoveToNextEP(bool current_success);
  void SetStagesProgressRatios(const QVector<double>& ratios);
  void SetCurrentStage(int current_stage);

  GuiProgressTrackerHandler* GetProgressHandler() const;

 signals:
  void ExecutionCancelRequested();

 private slots:
  void OnHideCountersToggled(bool checked);
  void OnSystemMonitoringWidgetVisibilityChanged(bool visible);
  void OnMonitoringTimerTimeout();
  void OnCancelClicked();
  void OnCurrentTaskInfoChanged(const QString& name, int progress,
                                int total_tasks, int current_task,
                                int total_steps, int current_step);
  void OnLogMessageAppended(const QString& message);

 private:
  QThread* monitoring_thread_;
  std::atomic_bool monitoring_update_in_progress_;
  QTimer* monitoring_timer_;
  QMap<QString, int> monitoring_widget_indices_;
  QMap<QString, bool> is_gpu_dedicated;

  GuiProgressTrackerHandler* progress_handler_;
  QVector<double> stages_progress_ratios_;
  int current_stage_;

  views::RealTimeMonitoringPage* GetView() const;
  ExecutionProgressWidget* GetExecutionProgressWidget() const;

  void StartMonitoring();
  void StopMonitoring();
  void InitializeMonitoringWidgets();
  void InitializeMonitoringThread();
};
}  // namespace controllers
}  // namespace gui

#endif  // __REALTIME_PAGE_CONTROLLER_H__