#ifndef BENCHMARK_CONTROLLER_H_
#define BENCHMARK_CONTROLLER_H_

#include <QObject>

#include "core/types.h"

namespace cil {
class ExecutionConfig;
class Unpacker;
class EPDependenciesManager;
}  // namespace cil

namespace gui {
namespace controllers {

class RealtimePageController;

/**
 * @class AppController
 * @brief Benchmark controller containing logic responsible for utilizing the
 * benchmark runner and controlling the benchmarking process, as well as
 * controlling the benchmarking page controller(RealtimePageController) during
 * the benchmark
 */
class BenchmarkController : public QObject {
  Q_OBJECT
 public:
  explicit BenchmarkController(
      RealtimePageController* benchmark_page_controller,
      std::atomic<bool>& interrupt, QObject* parent = nullptr);

  void SetUnpacker(std::shared_ptr<cil::Unpacker> unpacker);
  void SetConfigs(std::vector<std::shared_ptr<cil::ExecutionConfig>> configs);

  void SetDataDir(const std::string& dir);
  void SetOutputDir(const std::string& dir);

  void SetRunnerConfigs(const std::string& ep_deps_config_path,
                        const std::string& ep_deps_config_schema_path,
                        const std::string& input_schema_path,
                        const std::string& output_schema_path,
                        const std::string& verification_file_schema_path);

  void EnumerateDevices(const QList<EPInformationCard>& all_eps);
  void RunBenchmark(bool download_deps_only);
  void StopBenchmark(bool wait_for_finished);

  void ClearCache();

  const QList<EPInformationCard>& GetOverriddenEps() const;
  const std::vector<std::vector<nlohmann::json>>& GetPreparedEPsConfigs() const;
  const BenchmarkStatus& GetBenchmarkStatus() const;

 signals:
  void EnumerationFinished();
  void BenchmarkFinished(bool download_deps_only);
  void ClearCacheFinished();

 private slots:
  void EnumerateDevicesWorker();
  void BenchmarkWorker(bool download_deps_only);
  bool CollectRemoteSizesWorker();

 private:
  // Disable copy and assignment
  BenchmarkController(const BenchmarkController&) = delete;
  BenchmarkController& operator=(const BenchmarkController&) = delete;

  std::shared_ptr<cil::EPDependenciesManager> CreateEPDependenciesManager(
      std::shared_ptr<cil::ExecutionConfig> config);

  RealtimePageController* benchmark_page_controller_;
  std::shared_ptr<cil::Unpacker> unpacker_;
  std::vector<std::shared_ptr<cil::ExecutionConfig>> configs_;

  QList<EPInformationCard> eps_overridden_;
  std::vector<std::vector<nlohmann::json>> prepared_eps_configs_;

  std::string output_dir_;
  std::string data_dir_;

  std::string ep_dependencies_config_path_;
  std::string ep_dependencies_config_schema_path_;
  std::string input_file_schema_path_;
  std::string output_results_schema_path_;
  std::string data_verification_file_schema_path_;

  QThread* benchmark_thread_;
  std::atomic<bool>& interrupt_;

  BenchmarkStatus benchmark_status_;
};
}  // namespace controllers
}  // namespace gui

#endif  // BENCHMARK_CONTROLLER_H_