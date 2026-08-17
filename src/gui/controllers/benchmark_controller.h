#ifndef BENCHMARK_CONTROLLER_H_
#define BENCHMARK_CONTROLLER_H_

#include <QObject>
#include <map>

#include "../CIL/benchmark/scenario_data.h"
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
                        const std::string& image_input_schema_path,
                        const std::string& output_schema_path,
                        const std::string& verification_file_schema_path);

  void EnumerateDevices(const QList<EPInformationCard>& all_eps);
  void RunBenchmark(bool download_deps_only, bool ask_before_download);
  void StopBenchmark(bool wait_for_finished);

  void ClearCache();

  const QList<EPInformationCard>& GetOverriddenEps() const;
  const std::vector<std::vector<nlohmann::json>>& GetPreparedEPsConfigs() const;
  const BenchmarkStatus& GetBenchmarkStatus() const;

  // User-facing reason the last enumeration failed (currently set when the
  // upfront disk-space check fails); empty when enumeration succeeded.
  const QString& GetEnumerationErrorMessage() const;

 signals:
  /// Coarse progress (0-100) emitted per config during phase 3.
  void EnumerationProgressChanged(int progress);
  /// Per-config tick of the upfront size pre-pass (phase 1).
  void EnumerationSizingProgress(int configs_done, int configs_total);
  /// Byte-level download progress during phase 2.
  void EnumerationDownloadProgress(qint64 downloaded_bytes, qint64 total_bytes);
  /// Fired once between phase 2 (download) and phase 3 (device enumeration).
  void EnumerationPreparationPhaseStarted();
  void EnumerationFinished();
  void BenchmarkFinished(bool download_deps_only);
  void ClearCacheFinished();
  void DownloadDoNotAskAgainRequested();

 private slots:
  void EnumerateDevicesWorker();
  void BenchmarkWorker(bool download_deps_only);
  bool CollectRemoteSizesWorker(bool ask_before_download);

 private:
  // Disable copy and assignment
  BenchmarkController(const BenchmarkController&) = delete;
  BenchmarkController& operator=(const BenchmarkController&) = delete;

  std::shared_ptr<cil::EPDependenciesManager> CreateEPDependenciesManager(
      std::shared_ptr<cil::ExecutionConfig> config);

  /**
   * @brief Sizes the whole enumeration download (all EP dependencies that are
   * not already cached) with a HEAD-only pre-pass.
   * @return Total bytes that the enumeration download will fetch.
   */
  qint64 CollectEnumerationDownloadSize();

  RealtimePageController* benchmark_page_controller_;
  std::shared_ptr<cil::Unpacker> unpacker_;
  std::vector<std::shared_ptr<cil::ExecutionConfig>> configs_;

  QList<EPInformationCard> eps_overridden_;
  QList<EPInformationCard> eps_to_enumerate_;
  std::vector<std::vector<nlohmann::json>> prepared_eps_configs_;
  // Per-file size + planned destination for everything the enumeration
  // download will fetch, populated by CollectEnumerationDownloadSize().
  std::map<std::string, cil::FileInfo> enumeration_file_infos_;

  std::string output_dir_;
  std::string data_dir_;

  std::string ep_dependencies_config_path_;
  std::string ep_dependencies_config_schema_path_;
  std::string input_file_schema_path_;
  std::string image_input_file_schema_path_;
  std::string output_results_schema_path_;
  std::string data_verification_file_schema_path_;

  QThread* benchmark_thread_;
  std::atomic<bool>& interrupt_;

  BenchmarkStatus benchmark_status_;
  QString enumeration_error_message_;
};
}  // namespace controllers
}  // namespace gui

#endif  // BENCHMARK_CONTROLLER_H_