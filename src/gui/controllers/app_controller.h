#ifndef APP_CONTROLLER_H_
#define APP_CONTROLLER_H_

#include <QObject>

#include "core/types.h"

namespace cil {
class ExecutionConfig;
class Unpacker;
}  // namespace cil

namespace gui {
namespace controllers {

class StartPageController;
class RealtimePageController;
class ResultsHistoryPageController;
class ResultsReportPageController;
class SettingsPageController;
class BenchmarkController;

/**
 * @class AppController
 * @brief Main application controller for GUI and page controllers.
 */
class AppController : public QObject {
  Q_OBJECT
 public:
  explicit AppController(QObject* parent = nullptr);

  void Init();
  ~AppController();

  /**
   * @brief Accessors for page controllers.
   */
  StartPageController* GetStartPageController() const {
    return start_page_controller_;
  }
  RealtimePageController* GetRealtimePageController() const {
    return realtime_monitoring_page_controller_;
  }
  ResultsHistoryPageController* GetResultsHistoryPageController() const {
    return results_history_page_controller_;
  }
  ResultsReportPageController* GetResultsReportPageController() const {
    return results_report_page_controller_;
  }
  SettingsPageController* GetSettingsPageController() const {
    return settings_page_controller_;
  }

 public slots:
  void EulaAccepted();

 signals:
  void SwitchToPage(PageType page_type);
  void ShowGlobalPopup(const QString& message, bool is_progress = false);
  void UpdateProgressPopup(int progress);
  void HidePopup();

 private slots:
  /**
   * @brief Handles the event when benchmark device enumeration is finished.
   */
  void OnEnumerationFinished();

  /**
   * @brief Starts the benchmark process, optionally only downloading
   * dependencies.
   * @param download_deps_only If true, only downloads dependencies without
   * running the benchmark.
   */
  void RunBenchmark(bool download_deps_only);

  /**
   * @brief Stops the benchmark process, optionally waiting for completion.
   * @param wait_for_finished If true, waits for the benchmark to finish before
   * returning.
   */
  void StopBenchmark(bool wait_for_finished);

  /**
   * @brief Handles the event when the benchmark is finished.
   * @param download_deps_only true if only dependency download was performed,
   * false if the full benchmark ran.
   */
  void OnBenchmarkFinished(bool download_deps_only);

  /**
   * @brief Handles the event to cache clear request.
   */
  void OnClearCacheRequested();

  /**
   * @brief Handles the event when the cache clear operation is finished.
   */
  void OnClearCacheFinished();

  /**
   * @brief Handles the event when to keep logs setting change.
   * @param keep Boolean to keep logs or not.
   */
  void OnKeepLogsChanged(bool keep);

 private:
  // Disable copy and assignment
  AppController(const AppController&) = delete;
  AppController& operator=(const AppController&) = delete;

  /**
   * @brief Creates all page controllers and initializes their connections.
   */
  void CreateControllers();

  /**
   * @brief Installs signal handlers for inter-component communication.
   */
  void InstallSignalHandlers();

  /**
   * @brief Initializes logging resources and configuration.
   */
  void InitLogs();

  void InitConfigs();
  void InitStartPage();

  StartPageController* start_page_controller_;
  RealtimePageController* realtime_monitoring_page_controller_;
  ResultsHistoryPageController* results_history_page_controller_;
  ResultsReportPageController* results_report_page_controller_;
  SettingsPageController* settings_page_controller_;
  BenchmarkController* benchmark_controller_;

  std::vector<std::shared_ptr<cil::ExecutionConfig>> base_configs_;
  std::vector<std::shared_ptr<cil::ExecutionConfig>> configs_;
  std::shared_ptr<cil::Unpacker> unpacker_;
  std::string log_config_path_;
  std::string output_dir_;
  std::string config_schema_path_;

  std::atomic<bool> interrupt_;
};
}  // namespace controllers
}  // namespace gui

#endif  // APP_CONTROLLER_H_