#ifndef REALTIME_MONITORING_PAGE_H
#define REALTIME_MONITORING_PAGE_H

#include "abstract_view.h"

class ExecutionProgressWidget;
class QStackedWidget;
class SystemMonitoringWidget;
class LogsWidget;

namespace gui {
struct BenchmarkStatus;
namespace views {

/**
 * @class RealTimeMonitoringPage
 * @brief Page for real-time monitoring of benchmark execution and system
 * performance.
 */
class RealTimeMonitoringPage : public AbstractView {
  Q_OBJECT
 public:
  explicit RealTimeMonitoringPage(QWidget *parent = nullptr);

  /**
   * @brief Show/hide performance counters and metrics.
   */
  void SetHideCounters(bool hide);

  /**
   * @brief Show a confirmation pop-up for data download.
   * @param size_in_bytes size of the data to be downloaded, in bytes.
   * @param do_not_ask_again Output parameter. Indicates whether the user
   * selected an option to avoid being asked again in the future.
   * @return true if the user confirms, false otherwise.
   */
  bool RequestDownload(uint64_t size_in_bytes, bool &do_not_ask_again);

  /**
   * @brief Show a status dialog with benchmark result details.
   * @param action_type type of benchmark action.
   * @param status result details of the benchmark action.
   */
  void ShowStatus(const QString &action_type, const BenchmarkStatus &status);

  /**
   * @brief Access execution progress widget.
   */
  ExecutionProgressWidget *GetExecutionProgressWidget() const;

  /**
   * @brief Access logs widget.
   */
  LogsWidget *GetLogsWidget() const;

  /**
   * @brief Access system monitoring widget.
   */
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
