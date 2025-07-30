#ifndef APP_CONTROLLER_H_
#define APP_CONTROLLER_H_

#include <QObject>

#include "core/types.h"

namespace cil {
class ExecutionConfig;
class Unpacker;
class EPDependenciesManager;
}  // namespace cil

namespace gui {
namespace controllers {

class StartPageController;
class RealtimePageController;
class ResultsHistoryPageController;
class ResultsReportPageController;
class SettingsPageController;

class AppController : public QObject {
  Q_OBJECT
 public:
  explicit AppController(QObject* parent = nullptr);
  void Init();
  void InitStartPage();
  ~AppController();

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
  void ShowGlobalPopup(const QString& message);
  void HidePopup();

 private slots:
  void EnumerateDevices(const QList<EPInformationCard>& all_eps);
  void RunBenchmark(bool download_deps_only);
  void StopBenchmark(bool wait_for_finished);
  void EnumerateDevicesWorker();
  void BenchmarkWorker(bool download_deps_only);

  void OnClearCacheRequested();
  void OnKeepLogsChanged(bool keep);

 private:
  // Disable copy and assignment
  AppController(const AppController&) = delete;
  AppController& operator=(const AppController&) = delete;

  void CreateControllers();
  void InstallSignalHandlers();
  void InitLogs();

  std::shared_ptr<cil::EPDependenciesManager> CreateEPDependenciesManager(
      std::shared_ptr<cil::ExecutionConfig> config);

  StartPageController* start_page_controller_;
  RealtimePageController* realtime_monitoring_page_controller_;
  ResultsHistoryPageController* results_history_page_controller_;
  ResultsReportPageController* results_report_page_controller_;
  SettingsPageController* settings_page_controller_;

  std::vector<std::shared_ptr<cil::ExecutionConfig>> base_configs_;
  std::vector<std::vector<nlohmann::json>> prepared_eps_configs_;
  std::vector<std::shared_ptr<cil::ExecutionConfig>> configs_;
  std::shared_ptr<cil::Unpacker> unpacker_;
  std::string log_config_path_;
  std::string output_dir_;
  std::string data_dir_;
  std::string config_schema_path_;
  std::string ep_dependencies_config_path_;
  std::string ep_dependencies_config_schema_path_;
  std::string input_file_schema_path_;
  std::string output_results_schema_path_;
  std::string data_verification_file_schema_path_;

  QThread* benchmark_thread_;
  std::atomic<bool> interrupt_;

  // True if there was a failure in a benchmark stage of any config.
  bool had_failure_benchmarking_;
};
}  // namespace controllers
}  // namespace gui

#endif  // APP_CONTROLLER_H_