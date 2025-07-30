#ifndef REALTIME_MONITORING_PAGE_H
#define REALTIME_MONITORING_PAGE_H

#include "abstract_view.h"

class ExecutionProgressWidget;
class QStackedWidget;
class SystemMonitoringWidget;
class LogsWidget;

namespace gui {
namespace views {
class RealTimeMonitoringPage : public AbstractView {
  Q_OBJECT
 public:
  explicit RealTimeMonitoringPage(QWidget *parent = nullptr);
  void SetHideCounters(bool hide);

  ExecutionProgressWidget *GetExecutionProgressWidget() const;
  LogsWidget *GetLogsWidget() const;
  SystemMonitoringWidget *GetSystemMonitoringWidget() const;

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 private:
  ExecutionProgressWidget *execution_progress_widget_;
  QStackedWidget *stacked_widget_;
  SystemMonitoringWidget *system_monitoring_widget_;
  LogsWidget *logs_widget_;
};
}  // namespace views
}  // namespace gui

#endif  // REALTIME_MONITORING_PAGE_H
