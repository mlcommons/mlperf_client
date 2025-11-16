#include "realtime_monitoring_page.h"

#include <QDesktopServices>
#include <QStackedWidget>

#include "core/gui_utils.h"
#include "core/types.h"
#include "widgets/benchmark_status_widget.h"
#include "widgets/execution_progress_widget.h"
#include "widgets/logs_widget.h"
#include "widgets/popup_widget.h"
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
  main_layout->setContentsMargins(30, 25, 30, 25);
  main_layout->setSpacing(10);

  execution_progress_widget_ = new ExecutionProgressWidget(this);
  execution_progress_widget_->setMinimumWidth(300);
  main_layout->addWidget(execution_progress_widget_, 3);

  stacked_widget_ = new QStackedWidget(this);
  system_monitoring_widget_ = new SystemMonitoringWidget(stacked_widget_);
  logs_widget_ = new LogsWidget(stacked_widget_);

  stacked_widget_->addWidget(logs_widget_);
  stacked_widget_->addWidget(system_monitoring_widget_);

  main_layout->addWidget(stacked_widget_, 8);
}

void RealTimeMonitoringPage::InstallSignalHandlers() {}

void RealTimeMonitoringPage::SetHideCounters(bool hide) {
  stacked_widget_->setCurrentIndex(hide ? 0 : 1);
}

bool RealTimeMonitoringPage::RequestDownload(uint64_t size_in_bytes) {
  PopupWidget *popup = new PopupWidget(this, true);
  popup->setFixedSize(400, 250);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setModal(true);
  popup->SetMessage(
      QString("You are about to download\napproximately %1 of data.\nWould you "
              "like to proceed?")
          .arg(gui::utils::BytesToHumanReadableString(size_in_bytes)));

  return popup->exec() == QDialog::Accepted;
}

void RealTimeMonitoringPage::ShowStatus(const QString &action_type,
                                        const BenchmarkStatus &status) {
  BenchmarkStatusWidget *widget =
      new BenchmarkStatusWidget(action_type, status, this);
  if (widget->exec())
    QDesktopServices::openUrl(QUrl::fromLocalFile(status.logs_path_));
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