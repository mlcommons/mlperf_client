#include "benchmark_controller.h"

#include <QThread>

#include "../CIL/benchmark/runner.h"
#include "../CIL/benchmark_logger.h"
#include "../CIL/benchmark_result.h"
#include "../CIL/ep_dependencies_manager.h"
#include "../CIL/execution_config.h"
#include "../CIL/execution_provider.h"
#include "../CIL/utils.h"
#include "gui_progress_tracker_handler.h"
#include "realtime_page_controller.h"

using namespace log4cxx;

const LoggerPtr loggerBenchmarkController(
    Logger::getLogger("BenchmarkController"));

namespace gui {
namespace controllers {
BenchmarkController::BenchmarkController(
    RealtimePageController* benchmark_page_controller,
    std::atomic<bool>& interrupt, QObject* parent)
    : benchmark_page_controller_(benchmark_page_controller),
      interrupt_(interrupt),
      benchmark_thread_(nullptr) {}

void BenchmarkController::SetUnpacker(std::shared_ptr<cil::Unpacker> unpacker) {
  unpacker_ = unpacker;
}

void BenchmarkController::SetConfigs(
    std::vector<std::shared_ptr<cil::ExecutionConfig>> configs) {
  configs_ = configs;
}

void BenchmarkController::SetDataDir(const std::string& dir) {
  data_dir_ = dir;
}

void BenchmarkController::SetOutputDir(const std::string& dir) {
  output_dir_ = dir;
};

void BenchmarkController::SetRunnerConfigs(
    const std::string& ep_deps_config_path,
    const std::string& ep_deps_config_schema_path,
    const std::string& input_schema_path, const std::string& output_schema_path,
    const std::string& verification_file_schema_path) {
  ep_dependencies_config_path_ = ep_deps_config_path;
  ep_dependencies_config_schema_path_ = ep_deps_config_schema_path;
  input_file_schema_path_ = input_schema_path;
  output_results_schema_path_ = output_schema_path;
  data_verification_file_schema_path_ = verification_file_schema_path;
}

const QList<EPInformationCard>& BenchmarkController::GetOverriddenEps() const {
  return eps_overridden_;
}
const std::vector<std::vector<nlohmann::json>>&
BenchmarkController::GetPreparedEPsConfigs() const {
  return prepared_eps_configs_;
};

const BenchmarkStatus& BenchmarkController::GetBenchmarkStatus() const {
  return benchmark_status_;
}

void BenchmarkController::EnumerateDevices(
    const QList<EPInformationCard>& all_eps) {
  benchmark_status_ = BenchmarkStatus{
      false, false, false, QString::fromStdString(output_dir_), {}};
  benchmark_thread_ = QThread::create([this] { EnumerateDevicesWorker(); });
  connect(benchmark_thread_, &QThread::finished, benchmark_thread_,
          [all_eps, this]() {
            benchmark_thread_->deleteLater();
            benchmark_thread_ = nullptr;

            eps_overridden_ = all_eps;
            for (int i = 0; i < eps_overridden_.size(); ++i) {
              const auto& prepared_configs = prepared_eps_configs_.at(i);
              for (const auto& config : prepared_configs) {
                int device_id = config["device_id"];
                std::string device_name = config["device_name"];
                eps_overridden_[i].devices_
                    << QString::fromStdString(device_name) +
                           " (device_id = " + QString::number(device_id) + ")";
              }
            }
            emit EnumerationFinished();
          });

  benchmark_thread_->start();
}

void BenchmarkController::RunBenchmark(bool download_deps_only,
                                       bool ask_before_download) {
  if (benchmark_thread_) benchmark_thread_->wait();

  benchmark_status_ = BenchmarkStatus{
      false, false, false, QString::fromStdString(output_dir_), {}};
  benchmark_thread_ = QThread::create([this, download_deps_only,
                                       ask_before_download] {
    if (ask_before_download && !CollectRemoteSizesWorker(ask_before_download))
      return;
    BenchmarkWorker(download_deps_only);
  });
  connect(benchmark_thread_, &QThread::finished, benchmark_thread_,
          [this, download_deps_only]() {
            benchmark_thread_->deleteLater();
            benchmark_thread_ = nullptr;

            emit BenchmarkFinished(download_deps_only);
          });

  interrupt_ = false;
  benchmark_thread_->start();
}

void BenchmarkController::StopBenchmark(bool wait_for_finished) {
  if (benchmark_thread_ && benchmark_thread_->isRunning()) {
    interrupt_ = true;
    if (wait_for_finished) benchmark_thread_->wait();
  }
}

void BenchmarkController::BenchmarkWorker(bool download_deps_only) {
  benchmark_status_.success_ = true;
  int stages_per_ep = 0;
  for (int i = 0; i < configs_.size(); ++i) {
    if (interrupt_) break;
    QMetaObject::invokeMethod(benchmark_page_controller_, "SetEpSelected",
                              Qt::QueuedConnection, Q_ARG(int, i));
    const auto& config = configs_[i];
    auto ep_dependencies_manager = CreateEPDependenciesManager(config);

    cil::BenchmarkRunner::Params params;
    params.logger = loggerBenchmarkController;
    params.progress_handler = benchmark_page_controller_->GetProgressHandler();
    params.config = config.get();
    params.config->GetSystemConfig().SetDownloadBehavior("normal");
    params.unpacker = unpacker_.get();
    params.ep_dependencies_manager = ep_dependencies_manager.get();
    params.output_dir = output_dir_;
    params.data_dir = data_dir_;
    params.output_results_schema_path = output_results_schema_path_;
    params.input_file_schema_path = input_file_schema_path_;
    params.data_verification_file_schema_path =
        data_verification_file_schema_path_;

    EPBenchmarkStatus status{benchmark_page_controller_->GetEPDisplayName(i),
                             true, ""};
    try {
      auto benchmark_runner = std::make_unique<cil::BenchmarkRunner>(params);
      if (!stages_per_ep) {
        stages_per_ep = benchmark_runner->GetTotalStages();
        QVector<double> stages_progress_ratios = {0.25, 0.05, 0.05, 0.65};
        if (stages_per_ep && stages_per_ep != stages_progress_ratios.size())
          stages_progress_ratios.resize(stages_per_ep, 1.0 / stages_per_ep);
        QMetaObject::invokeMethod(
            benchmark_page_controller_, "SetStagesProgressRatios",
            Qt::QueuedConnection,
            Q_ARG(QVector<double>, stages_progress_ratios));
      }

      auto on_failed_stage_callback = [&](const std::string_view& stage_name,
                                          int) {
        std::string error_string;
        if (stage_name == "Download") {
          error_string = "Failed to download necessary files, skipping...\n";
        } else if (stage_name == "Preparation") {
          error_string = "Inferences preparation failed, skipping...\n";
        } else if (stage_name == "DataVerification") {
          LOG4CXX_WARN(loggerBenchmarkController,
                       "Data verification failed...\n");

          // We continue the scenario even if files are not fully verified
          return true;
        } else if (stage_name == "Benchmark") {
          error_string = "Failed to run benchmark...\n";
        }
        LOG4CXX_ERROR(loggerBenchmarkController, error_string);
        status.error_message_ = QString::fromStdString(error_string);
        status.success_ = false;
        return false;
      };

      auto on_enter_stage_callback = [&](const std::string_view& stage_name,
                                         int) {
        if (download_deps_only && stage_name == "Preparation") {
          // Stop the benchmark process if we are just donwnloading the deps
          return false;
        }

        int current_stage_index =
            benchmark_runner->GetStageIndex(std::string(stage_name));
        QMetaObject::invokeMethod(benchmark_page_controller_, "SetCurrentStage",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, current_stage_index));
        return true;
      };

      benchmark_runner->SetOnEnterStageCallback(on_enter_stage_callback);
      benchmark_runner->SetOnFailedStageCallback(on_failed_stage_callback);

      bool run_success = benchmark_runner->Run();
      const auto& results = benchmark_runner->GetResultsLogger().GetResults();
      if (!download_deps_only)
        status.success_ = run_success && !results.empty() &&
                          results.front().benchmark_success;
      if (!results.empty()) {
        const auto& res = results.front();
        if (!res.error_message.empty()) {
          status.error_message_ += QString::fromStdString(
              cil::utils::CleanErrorMessageFromStaticPaths(res.error_message));
        }
        if (!res.ep_error_messages.empty()) {
          status.error_message_ +=
              "\n" + QString::fromStdString(res.ep_error_messages);
        }
      }
    } catch (const std::exception& e) {
      std::string error_message =
          std::string("Exception while running the benchmark: ") + e.what();
      status.error_message_ += QString::fromStdString(error_message);
      status.success_ = false;
      LOG4CXX_ERROR(loggerBenchmarkController, error_message);
    }
    QMetaObject::invokeMethod(benchmark_page_controller_, "MoveToNextEP",
                              Qt::QueuedConnection,
                              Q_ARG(bool, status.success_));
    benchmark_status_.eps_benchmark_status_.emplace_back(status);
    benchmark_status_.success_ &= status.success_;
  }
}

bool BenchmarkController::CollectRemoteSizesWorker(bool ask_before_download) {
  QMetaObject::invokeMethod(
      benchmark_page_controller_,
      [&]() {
        benchmark_page_controller_->AppendLogMessage(
            "Calculating the total amount of data to be downloaded...");
        benchmark_page_controller_->SetCurrentTaskName("Calculating");
        benchmark_page_controller_->EnableLogs(false);
      },
      Qt::BlockingQueuedConnection);

  auto progress_handler =
      std::make_unique<cil::ProgressTrackerHandler>(interrupt_);
  progress_handler->SetForceAcceptInterruptRequest(true);

  std::map<std::string, uint64_t> remote_files;
  bool collected = true;
  for (int i = 0; i < configs_.size(); ++i) {
    if (interrupt_) break;
    const auto& config = configs_[i];
    auto ep_dependencies_manager = CreateEPDependenciesManager(config);

    cil::BenchmarkRunner::Params params;
    params.logger = loggerBenchmarkController;
    params.progress_handler = progress_handler.get();
    params.config = config.get();
    params.config->GetSystemConfig().SetDownloadBehavior("normal");
    params.unpacker = unpacker_.get();
    params.ep_dependencies_manager = ep_dependencies_manager.get();
    params.data_dir = data_dir_;
    params.collect_remote_sizes_only = true;

    try {
      auto benchmark_runner = std::make_unique<cil::BenchmarkRunner>(params);

      benchmark_runner->SetOnEnterStageCallback(
          [&](const std::string_view& stage_name, int) {
            return stage_name == "Download";
          });
      benchmark_runner->SetOnFailedStageCallback(
          [&](const std::string_view& stage_name, int) {
            if (stage_name == "Download") {
              LOG4CXX_ERROR(loggerBenchmarkController,
                            "Failed to collect remote files size\n");
              collected = false;
            }
            return false;
          });

      benchmark_runner->Run();

      const auto& sizes_map = benchmark_runner->GetRemoteFileSizes();
      remote_files.insert(sizes_map.begin(), sizes_map.end());
    } catch (const std::exception& e) {
      std::string error_message =
          "Exception while collecting remote files size: ";
      LOG4CXX_ERROR(loggerBenchmarkController, error_message + e.what());
      collected = false;
    }
    if (!collected) break;
  }

  QMetaObject::invokeMethod(
      benchmark_page_controller_,
      [&]() { benchmark_page_controller_->EnableLogs(true); },
      Qt::BlockingQueuedConnection);

  benchmark_status_.size_info_collected_ = collected;
  if (!collected) return false;

  uint64_t remote_files_size = 0;
  for (const auto& [path, file_size] : remote_files)
    remote_files_size += file_size;

  if (remote_files_size == 0) return true;

  bool download_do_not_ask_again = false;
  QMetaObject::invokeMethod(
      benchmark_page_controller_,
      [&]() {
        benchmark_status_.download_accepted_ =
            benchmark_page_controller_->RequestDownload(
                remote_files_size, download_do_not_ask_again);
      },
      Qt::BlockingQueuedConnection);

  if (download_do_not_ask_again && benchmark_status_.download_accepted_)
    emit DownloadDoNotAskAgainRequested();

  return benchmark_status_.download_accepted_;
}

void BenchmarkController::EnumerateDevicesWorker() {
  benchmark_status_.success_ = true;
  for (int i = 0; i < configs_.size(); ++i) {
    const auto& config = configs_[i];
    if (interrupt_) break;
    auto ep_dependencies_manager = CreateEPDependenciesManager(config);
    auto progress_handler =
        std::make_unique<cil::ProgressTrackerHandler>(interrupt_);
    progress_handler->SetForceAcceptInterruptRequest(true);

    cil::BenchmarkRunner::Params params;
    params.logger = loggerBenchmarkController;
    params.progress_handler = progress_handler.get();
    params.config = config.get();
    params.unpacker = unpacker_.get();
    params.ep_dependencies_manager = ep_dependencies_manager.get();
    params.enumerate_only = true;

    try {
      auto benchmark_runner = std::make_unique<cil::BenchmarkRunner>(params);

      auto on_enter_stage_callback = [&](const std::string_view& stage_name,
                                         int) {
        return stage_name == "Preparation" || stage_name == "Download";
      };

      benchmark_runner->SetOnEnterStageCallback(on_enter_stage_callback);

      auto on_failed_stage_callback = [&](const std::string_view& stage_name,
                                          int) {
        if (stage_name == "Download") {
          LOG4CXX_ERROR(loggerBenchmarkController,
                        "Failed to download necessary files for devices "
                        "enumeration...\n");
        } else if (stage_name == "Preparation") {
          LOG4CXX_ERROR(loggerBenchmarkController,
                        "Devices enumeration failed...\n");
        }
        benchmark_status_.success_ = false;
        return false;
      };

      benchmark_runner->SetOnFailedStageCallback(on_failed_stage_callback);

      benchmark_runner->Run();

      auto prepared_eps = benchmark_runner->GetPreparedEPs();
      std::vector<nlohmann::json> json_configs;
      if (!prepared_eps.empty()) {
        for (const auto& ep : prepared_eps.front())
          json_configs.push_back(ep.first.GetConfig());
        prepared_eps_configs_.push_back(json_configs);
      }
    } catch (const std::exception& e) {
      std::string error_message = "Exception while enumerating devices: ";
      LOG4CXX_ERROR(loggerBenchmarkController, error_message + e.what());
      benchmark_status_.success_ = false;
    }
    emit EnumerationProgressChanged((i + 1) * 100 / configs_.size());
  }
}

void BenchmarkController::ClearCache() {
  benchmark_thread_ = QThread::create([this] {
    cil::BenchmarkRunner::ClearCache(unpacker_->GetDepsDir());
    try {
      std::filesystem::remove_all(data_dir_);
    } catch (const std::filesystem::filesystem_error& e) {
      LOG4CXX_ERROR(
          loggerBenchmarkController,
          "Failed to clear data directory: " << data_dir_ << ", " << e.what());
    }
  });
  connect(benchmark_thread_, &QThread::finished, benchmark_thread_, [this]() {
    benchmark_thread_->deleteLater();
    benchmark_thread_ = nullptr;

    emit ClearCacheFinished();
  });
  benchmark_thread_->start();
}

std::shared_ptr<cil::EPDependenciesManager>
BenchmarkController::CreateEPDependenciesManager(
    std::shared_ptr<cil::ExecutionConfig> config) {
  const auto& ep =
      config->GetScenarios().front().GetExecutionProviders().front();

  auto is_valid_ep = cil::IsValidEP(ep.GetName());
  auto empty_library_path = ep.GetLibraryPath().empty();

  std::unordered_map<std::string, std::string> eps_dependencies_dest;
  if (empty_library_path && is_valid_ep) {
    std::string ep_deps_dir = unpacker_->GetDepsDir();

    eps_dependencies_dest.insert(
        {ep.GetName(),
         cil::GetExecutionProviderParentLocation(ep, ep_deps_dir).string()});
  }

  auto ep_dependencies_manager = std::make_shared<cil::EPDependenciesManager>(
      eps_dependencies_dest, ep_dependencies_config_path_,
      ep_dependencies_config_schema_path_, unpacker_->GetDepsDir());
  ep_dependencies_manager->Initialize();
  return ep_dependencies_manager;
}

}  // namespace controllers
}  // namespace gui