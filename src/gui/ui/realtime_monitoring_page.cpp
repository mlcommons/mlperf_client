#include "realtime_monitoring_page.h"

#include <QStackedWidget>

#include "widgets/execution_progress_widget.h"
#include "widgets/logs_widget.h"
#include "widgets/system_monitoring_widget.h"

namespace gui {
namespace views {
RealTimeMonitoringPage::RealTimeMonitoringPage(QWidget *parent)
    : AbstractView(parent),
      execution_progress_widget_(nullptr),
      stacked_widget_(nullptr),
      system_monitoring_widget_(nullptr),
      logs_widget_(nullptr) {}

void RealTimeMonitoringPage::SetupUi() {
  QHBoxLayout *main_layout = new QHBoxLayout(this);
  main_layout->setContentsMargins(102, 25, 102, 25);
  main_layout->setSpacing(10);

  execution_progress_widget_ = new ExecutionProgressWidget(this);
  main_layout->addWidget(execution_progress_widget_, 2);

  stacked_widget_ = new QStackedWidget(this);
  system_monitoring_widget_ = new SystemMonitoringWidget(stacked_widget_);
  logs_widget_ = new LogsWidget(stacked_widget_);

  stacked_widget_->addWidget(logs_widget_);
  stacked_widget_->addWidget(system_monitoring_widget_);

  main_layout->addWidget(stacked_widget_, 5);
}

void RealTimeMonitoringPage::InstallSignalHandlers() {}

void RealTimeMonitoringPage::SetHideCounters(bool hide) {
  stacked_widget_->setCurrentIndex(hide ? 0 : 1);
}

ExecutionProgressWidget *RealTimeMonitoringPage::GetExecutionProgressWidget()
    const {
  return execution_progress_widget_;
}

LogsWidget *RealTimeMonitoringPage::GetLogsWidget() const {
  return logs_widget_;
}

SystemMonitoringWidget *RealTimeMonitoringPage::GetSystemMonitoringWidget()
    const {
  return system_monitoring_widget_;
}

}  // namespace views
}  // namespace gui