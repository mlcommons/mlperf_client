#include "app_controller.h"

#include <log4cxx/fileappender.h>
#include <log4cxx/logger.h>
#include <log4cxx/logmanager.h>
#include <log4cxx/xml/domconfigurator.h>

#include <QThread>
#include <fstream>

#include "../CIL/benchmark/runner.h"
#include "../CIL/benchmark/scenario_data.h"
#include "../CIL/benchmark_logger.h"
#include "../CIL/ep_dependencies_manager.h"
#include "../CIL/execution_config.h"
#include "../CIL/execution_provider.h"
#include "../CIL/utils.h"
#include "core/gui_utils.h"
#include "gui_progress_tracker_handler.h"
#include "realtime_page_controller.h"
#include "results_history_page_controller.h"
#include "results_report_page_controller.h"
#include "settings_manager.h"
#include "settings_page_controller.h"
#include "start_page_controller.h"

#ifdef Q_OS_IOS
#include "ios/ios_utils.h"
#endif

using namespace log4cxx;
using namespace log4cxx::xml;

const LoggerPtr loggerAppController(Logger::getLogger("SystemController"));

namespace {}  // namespace

namespace gui {
namespace controllers {
AppController::AppController(QObject* parent)
    : start_page_controller_(nullptr),
      realtime_monitoring_page_controller_(nullptr),
      results_history_page_controller_(nullptr),
      results_report_page_controller_(nullptr),
      settings_page_controller_(nullptr),
      benchmark_thread_(nullptr),
      interrupt_(false),
      had_failure_benchmarking_(false) {
  CreateControllers();
  InstallSignalHandlers();
}

void AppController::Init() {
  if (!SettingsManager::getInstance().IsEulaAccepted()) {
    SwitchToPage(PageType::kEulaPage);
    return;
  }
  InitStartPage();
}

void AppController::InitStartPage() {
  unpacker_ = std::make_shared<cil::Unpacker>();

  InitLogs();

  std::string deps_dir = unpacker_->GetDepsDir();
  if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kConfigSchema,
                              config_schema_path_))
    LOG4CXX_ERROR(loggerAppController, "Failed to unpack config schema file!");

  std::string config_verification_file_path;
  if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kConfigVerificationFile,
                              config_verification_file_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack config verification file!");
  }

  std::string config_experimental_verification_file_path;
  if (!unpacker_->UnpackAsset(
          cil::Unpacker::Asset::kConfigExperimentalVerificationFile,
          config_experimental_verification_file_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack config experimental verification file!");
  }

  if (ep_dependencies_config_path_.empty() &&
      !unpacker_->UnpackAsset(cil::Unpacker::Asset::kEPDependenciesConfig,
                              ep_dependencies_config_path_)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack EP dependencies config!"
                      << ep_dependencies_config_path_);
  }

  if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kEPDependenciesConfigSchema,
                              ep_dependencies_config_schema_path_)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack EP dependencies config schema!");
  }

  std::string vendors_default_zip_path;
  if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kVendorsDefault,
                              vendors_default_zip_path))
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack vendors default zip file!");

  auto configs_folder_path = deps_dir + "/vendors_default";
  std::filesystem::remove_all(configs_folder_path);
  auto config_paths = unpacker_->UnpackFilesFromZIP(vendors_default_zip_path,
                                                    configs_folder_path, true);

  QList<EPInformationCard> all_eps;
  bool has_llama2_scenario = false;
  for (const auto& entry : config_paths) {
    auto config = std::make_shared<cil::ExecutionConfig>();
    bool json_loaded = config->ValidateAndParse(
        configs_folder_path + "/" + entry, config_schema_path_,
        config_verification_file_path,
        config_experimental_verification_file_path);
    if (json_loaded) {
      const auto& scenarios = config->GetScenarios();
      if (!scenarios.empty() &&
          !scenarios.front().GetExecutionProviders().empty()) {
        const auto scenario = scenarios.front();
        const auto& ep_config = scenario.GetExecutionProviders().front();
        std::string entry_file_name =
            std::filesystem::path(entry).stem().string();
        if (!cil::utils::IsEpConfigSupportedOnThisPlatform(entry_file_name) ||
            !cil::utils::IsEpSupportedOnThisPlatform("", ep_config.GetName()))
          continue;
        QString name = QString::fromStdString(ep_config.GetName());
        QString ep_display_name = QString::fromStdString(
            cil::EPNameToDisplayName(ep_config.GetName()));
        QString card_name = gui::utils::EPCardName(
            QString::fromStdString(entry_file_name), name);
        bool support_long_prompts =
            (entry.find("LongPrompts") != std::string::npos);
        all_eps.push_back({
            name,
            card_name,
            card_name.split(' ').back(),
            QString::fromStdString(config->GetSystemConfig().GetComment()),
            QString::fromStdString(scenario.GetName()),
            {},
            ep_config.GetConfig(),
            config->IsConfigExperimental(),
            support_long_prompts,
            ep_display_name,
        });
        base_configs_.push_back(config);
        if (cil::utils::StringToLowerCase(scenario.GetName()) == "llama2")
          has_llama2_scenario = true;
      }
    } else {
      LOG4CXX_ERROR(loggerAppController,
                    "Unable to load config file " << entry);
    }
  }
  if (has_llama2_scenario &&
      !unpacker_->UnpackAsset(cil::Unpacker::Asset::kLLMInputFileSchema,
                              input_file_schema_path_)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack llama2 input file schema!");
  }
  if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kOutputResultsSchema,
                              output_results_schema_path_)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack output results schema!");
  }
  if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kDataVerificationFileSchema,
                              data_verification_file_schema_path_)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack data verification file schema!");
  }

  start_page_controller_->LoadSystemInformation();

  emit SwitchToPage(PageType::kStartPage);
  emit ShowGlobalPopup("Downloading benchmark assets.\nPlease wait.");

  EnumerateDevices(all_eps);
}

AppController::~AppController() { StopBenchmark(true); }

void AppController::EulaAccepted() {
  SettingsManager::getInstance().EulaAccepted();
  InitStartPage();
}

void AppController::CreateControllers() {
  start_page_controller_ = new StartPageController(this);
  realtime_monitoring_page_controller_ =
      new RealtimePageController(interrupt_, this);
  results_history_page_controller_ = new ResultsHistoryPageController(this);
  results_report_page_controller_ = new ResultsReportPageController(this);

  QString default_data_path =
      QString::fromStdString(cil::utils::GetAppDefaultDataPath().string());
  settings_page_controller_ = new SettingsPageController(
      default_data_path + "/data", default_data_path, this);
}

void AppController::InstallSignalHandlers() {
  connect(start_page_controller_, &StartPageController::StartBenchmark, this,
          &AppController::RunBenchmark);

  connect(realtime_monitoring_page_controller_,
          &RealtimePageController::ExecutionCancelRequested, this, [this]() {
            emit ShowGlobalPopup("Canceling. Please wait.");
            StopBenchmark(false);
          });

  connect(results_history_page_controller_,
          &ResultsHistoryPageController::OpenReportRequested, this, [this]() {
            results_report_page_controller_->LoadResultsTable(
                results_history_page_controller_->GetCurrentEntries());
            emit SwitchToPage(PageType::kReportPage);
          });
  connect(results_report_page_controller_,
          &ResultsReportPageController::ReturnBackRequested, this,
          [this]() { emit SwitchToPage(PageType::kHistoryPage); });
  connect(results_report_page_controller_,
          &ResultsReportPageController::RunNewBenchmarkRequested, this,
          [this]() { emit SwitchToPage(PageType::kStartPage); });
  connect(settings_page_controller_, &SettingsPageController::LogsPathChanged,
          [this]() { InitLogs(); });
  connect(settings_page_controller_, &SettingsPageController::KeepLogsChanged,
          this, &AppController::OnKeepLogsChanged);
  connect(settings_page_controller_,
          &SettingsPageController::ClearCacheRequested, this,
          &AppController::OnClearCacheRequested);
}

void AppController::InitLogs() {
  std::string logs_parent_dir =
      settings_page_controller_->GetLogsPath().toStdString();
  output_dir_ = logs_parent_dir + "/Logs";
  if (log_config_path_.empty()) {
    if (!unpacker_->UnpackAsset(cil::Unpacker::Asset::kLog4cxxConfig,
                                log_config_path_)) {
      qDebug() << "Failed to unpack logger configuration file!";
    }
  }

  if (!fs::exists(output_dir_)) {
    try {
      fs::create_directories(output_dir_);
    } catch (const fs::filesystem_error& e) {
      qDebug() << "Failed to create the Logs directory: " << e.what();
    }
  }

  std::string prev_dir = cil::utils::GetCurrentDirectory();
  cil::utils::SetCurrentDirectory(logs_parent_dir);
  auto log_configure__status = DOMConfigurator::configure(log_config_path_);
  cil::utils::SetCurrentDirectory(prev_dir);

  if (log_configure__status != spi::ConfigurationStatus::Configured) {
    qDebug() << "Failed to configure the logger with default "
                "configurations...";
  }
  OnKeepLogsChanged(settings_page_controller_->GetKeepLogs());

  results_history_page_controller_->LoadHistory(output_dir_);
}

void AppController::OnKeepLogsChanged(bool keep) {
  auto loggers = LogManager::getLoggerRepository()->getCurrentLoggers();
  loggers.push_back(Logger::getRootLogger());

  for (auto logger : loggers) {
    if (logger->getName() == "Results") continue;
    auto appenders = logger->getAllAppenders();
    for (auto appender : appenders)
      if (auto file_appender = log4cxx::cast<log4cxx::FileAppender>(appender))
        file_appender->setThreshold(keep ? Level::getAll() : Level::getOff());
  }
}

void AppController::EnumerateDevices(const QList<EPInformationCard>& all_eps) {
  benchmark_thread_ = QThread::create([this] { EnumerateDevicesWorker(); });
  connect(benchmark_thread_, &QThread::finished, benchmark_thread_,
          [all_eps, this]() {
            benchmark_thread_->deleteLater();
            benchmark_thread_ = nullptr;

            auto all_eps_overriden = all_eps;
            for (int i = 0; i < all_eps_overriden.size(); ++i) {
              const auto& prepared_configs = prepared_eps_configs_.at(i);
              for (const auto& config : prepared_configs) {
                int device_id = config["device_id"];
                std::string device_name = config["device_name"];
                all_eps_overriden[i].devices_
                    << QString::fromStdString(device_name) +
                           " (device_id = " + QString::number(device_id) + ")";
              }
            }

            auto schema =
                cil::ExecutionConfig::GetEPsConfigSchema(config_schema_path_);
            start_page_controller_->LoadEPsInformation(schema,
                                                       all_eps_overriden);

            emit HidePopup();
          });

  benchmark_thread_->start();
}

void AppController::RunBenchmark(bool download_deps_only) {
  if (benchmark_thread_) benchmark_thread_->wait();

  had_failure_benchmarking_ = false;
  benchmark_thread_ = QThread::create(
      [this, download_deps_only] { BenchmarkWorker(download_deps_only); });
  connect(benchmark_thread_, &QThread::finished, benchmark_thread_,
          [this, download_deps_only]() {
            benchmark_thread_->deleteLater();
            benchmark_thread_ = nullptr;

#ifdef Q_OS_IOS
            gui::ios_utils::SetIdleTimerDisabled(false);
#endif

            // navigate back to the start page...
            emit SwitchToPage(PageType::kStartPage);
            start_page_controller_->ResetSelectedEPsConfigurations();
            results_history_page_controller_->LoadHistory(output_dir_);

            if (download_deps_only && !interrupt_) {
              // ...then show a notification popup
              emit ShowGlobalPopup(had_failure_benchmarking_
                                       ? "Files download failed.\nSee logs "
                                         "for more information."
                                       : "Files downloaded successfully.");
            } else {
              // ...then if not downloading only, open the history page
              if (!download_deps_only)
                emit SwitchToPage(PageType::kHistoryPage);
              if (interrupt_) emit HidePopup();
            }
          });

  auto eps_configs = start_page_controller_->GetCurrentEPsConfigurations();
  configs_.clear();
  QList<EPInformationCard> eps;
  eps.reserve(eps_configs.size());
  for (auto& ep_config : eps_configs) {
    auto config = std::make_shared<cil::ExecutionConfig>(
        *base_configs_.at(ep_config.first));
    auto device_it = ep_config.second.config_.find("Device");
    std::string device_str = device_it.value();
    auto device_index =
        ep_config.second.devices_.indexOf(QString::fromStdString(device_str));

    ep_config.second.config_.erase(device_it);
    /*bool ep_config_is_same =
        ep_config.second.config_ == config->GetScenarios()
                                        .front()
                                        .GetExecutionProviders()
                                        .front()
                                        .GetConfig();
    config->SetConfigVerified(ep_config_is_same);*/

    if (device_index != -1) {
      ep_config.second.config_["device_id"] =
          prepared_eps_configs_.at(ep_config.first)
              .at(device_index)["device_id"];
      ep_config.second.config_["device_name"] =
          prepared_eps_configs_.at(ep_config.first)
              .at(device_index)["device_name"];
    }

    config->GetScenarios().front().SetExecutionProviderConfig(
        0, ep_config.second.config_);
    configs_.push_back(config);
    eps.append(ep_config.second);
  }
  realtime_monitoring_page_controller_->InitExecutionWithEPs(eps);

#ifdef Q_OS_IOS
  gui::ios_utils::SetIdleTimerDisabled(true);
#endif
  interrupt_ = false;
  benchmark_thread_->start();

  emit SwitchToPage(PageType::kRealTimeMonitoringPage);
}

void AppController::StopBenchmark(bool wait_for_finished) {
  if (benchmark_thread_ && benchmark_thread_->isRunning()) {
    interrupt_ = true;
    if (wait_for_finished) benchmark_thread_->wait();
  }
}

void AppController::BenchmarkWorker(bool download_deps_only) {
  int stages_per_ep = 0;
  for (int i = 0; i < configs_.size(); ++i) {
    if (interrupt_) break;
    QMetaObject::invokeMethod(realtime_monitoring_page_controller_,
                              "SetEpSelected", Qt::QueuedConnection,
                              Q_ARG(int, i));
    const auto& config = configs_[i];
    auto ep_dependencies_manager = CreateEPDependenciesManager(config);

    cil::BenchmarkRunner::Params params;
    params.logger = loggerAppController;
    params.progress_handler =
        realtime_monitoring_page_controller_->GetProgressHandler();
    params.config = config.get();
    params.config->GetSystemConfig().SetDownloadBehavior("normal");
    params.unpacker = unpacker_.get();
    params.ep_dependencies_manager = ep_dependencies_manager.get();
    params.output_dir = output_dir_;
    params.data_dir = settings_page_controller_->GetDataPath().toStdString();
    params.output_results_schema_path = output_results_schema_path_;
    params.input_file_schema_path = input_file_schema_path_;
    params.data_verification_file_schema_path =
        data_verification_file_schema_path_;

    bool benchmark_success = false;
    try {
      auto benchmark_runner = std::make_unique<cil::BenchmarkRunner>(params);
      if (!stages_per_ep) {
        stages_per_ep = benchmark_runner->GetTotalStages();
        QVector<double> stages_progress_ratios = {0.25, 0.05, 0.05, 0.65};
        if (stages_per_ep && stages_per_ep != stages_progress_ratios.size())
          stages_progress_ratios.resize(stages_per_ep, 1.0 / stages_per_ep);
        QMetaObject::invokeMethod(
            realtime_monitoring_page_controller_, "SetStagesProgressRatios",
            Qt::QueuedConnection,
            Q_ARG(QVector<double>, stages_progress_ratios));
      }

      auto on_failed_stage_callback = [&](const std::string_view& stage_name,
                                          int) {
        had_failure_benchmarking_ = true;
        if (stage_name == "Download") {
          LOG4CXX_ERROR(loggerAppController,
                        "Failed to download necessary files, skipping...\n");
        } else if (stage_name == "Preparation") {
          LOG4CXX_ERROR(loggerAppController,
                        "Inferences preparation failed, skipping...\n");
        } else if (stage_name == "DataVerification") {
          LOG4CXX_WARN(loggerAppController, "Data verification failed...\n");

          // We continue the scenario even if files are not fully verified
          return true;
        } else if (stage_name == "Benchmark") {
          LOG4CXX_ERROR(loggerAppController, "Failed to run benchmark...\n");
        }
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
        QMetaObject::invokeMethod(realtime_monitoring_page_controller_,
                                  "SetCurrentStage", Qt::QueuedConnection,
                                  Q_ARG(int, current_stage_index));
        return true;
      };

      benchmark_runner->SetOnEnterStageCallback(on_enter_stage_callback);
      benchmark_runner->SetOnFailedStageCallback(on_failed_stage_callback);

      bool run_success = benchmark_runner->Run();
      const auto& results = benchmark_runner->GetResultsLogger().GetResults();
      benchmark_success = run_success && !results.empty() &&
                          results.front().error_message.empty();
    } catch (const std::exception& e) {
      std::string error_message = "Exception while running the benchmark: ";
      LOG4CXX_ERROR(loggerAppController, error_message + e.what());
    }
    QMetaObject::invokeMethod(realtime_monitoring_page_controller_,
                              "MoveToNextEP", Qt::QueuedConnection,
                              Q_ARG(bool, benchmark_success));
  }
}

void AppController::EnumerateDevicesWorker() {
  for (auto& config : base_configs_) {
    if (interrupt_) break;
    auto ep_dependencies_manager = CreateEPDependenciesManager(config);
    auto progress_handler =
        std::make_unique<cil::ProgressTrackerHandler>(interrupt_);
    progress_handler->SetForceAcceptInterruptRequest(true);

    cil::BenchmarkRunner::Params params;
    params.logger = loggerAppController;
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
          LOG4CXX_ERROR(loggerAppController,
                        "Failed to download necessary files for devices "
                        "enumeration...\n");
        } else if (stage_name == "Preparation") {
          LOG4CXX_ERROR(loggerAppController, "Devices enumeration failed...\n");
        }
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
      LOG4CXX_ERROR(loggerAppController, error_message + e.what());
    }
  }
}

void AppController::OnClearCacheRequested() {
  // make sure that the benchmark thread is not running
  StopBenchmark(true);

  emit ShowGlobalPopup("Clearing the cache. Please wait.");

  benchmark_thread_ = QThread::create([this] {
    std::string deps_dir;

    deps_dir = unpacker_->GetDepsDir();

    cil::BenchmarkRunner::ClearCache(deps_dir);
    auto data_path = settings_page_controller_->GetDataPath().toStdString();
    try {
      std::filesystem::remove_all(data_path);
    } catch (const std::filesystem::filesystem_error& e) {
      LOG4CXX_ERROR(loggerAppController, "Failed to clear data directory: "
                                             << data_path << ", " << e.what());
    }
  });
  connect(benchmark_thread_, &QThread::finished, benchmark_thread_, [this]() {
    benchmark_thread_->deleteLater();
    benchmark_thread_ = nullptr;

    emit HidePopup();
  });

  benchmark_thread_->start();
}

std::shared_ptr<cil::EPDependenciesManager>
AppController::CreateEPDependenciesManager(
    std::shared_ptr<cil::ExecutionConfig> config) {
  const auto& ep =
      config->GetScenarios().front().GetExecutionProviders().front();

  auto is_valid_ep = cil::IsValidEP(ep.GetName());
  auto empty_library_path = ep.GetLibraryPath().empty();

  std::unordered_map<std::string, std::string> eps_dependencies_dest;
  if (empty_library_path && is_valid_ep)
    eps_dependencies_dest.insert(
        {ep.GetName(),
         cil::GetExecutionProviderParentLocation(ep, unpacker_->GetDepsDir())
             .string()});

  auto ep_dependencies_manager = std::make_shared<cil::EPDependenciesManager>(
      eps_dependencies_dest, ep_dependencies_config_path_,
      ep_dependencies_config_schema_path_, unpacker_->GetDepsDir());
  ep_dependencies_manager->Initialize();
  return ep_dependencies_manager;
}

}  // namespace controllers
}  // namespace gui