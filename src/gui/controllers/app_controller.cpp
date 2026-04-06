#include "app_controller.h"

#include <log4cxx/fileappender.h>
#include <log4cxx/logger.h>
#include <log4cxx/logmanager.h>
#include <log4cxx/xml/domconfigurator.h>

#include "../CIL/execution_config.h"
#include "../CIL/execution_provider.h"
#include "../CIL/unpacker.h"
#include "../CIL/utils.h"
#include "benchmark_controller.h"
#include "core/gui_utils.h"
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

namespace gui {
namespace controllers {
AppController::AppController(QObject* parent)
    : start_page_controller_(nullptr),
      realtime_monitoring_page_controller_(nullptr),
      results_history_page_controller_(nullptr),
      results_report_page_controller_(nullptr),
      settings_page_controller_(nullptr),
      interrupt_(false) {
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

void AppController::InitConfigs() {
  using Asset = cil::Unpacker::Asset;
  if (!unpacker_->UnpackAsset(Asset::kConfigSchema, config_schema_path_))
    LOG4CXX_ERROR(loggerAppController, "Failed to unpack config schema file!");

  std::map<Asset, std::string> config_verification_assets = {
      {Asset::kConfigVerificationFile, ""},
      {Asset::kConfigExperimentalVerificationFile, "experimental"},
      {Asset::kConfigExtendedVerificationFile, "extended"}};

  std::map<std::string, std::string> config_verification_files;
  for (const auto& [asset, category] : config_verification_assets)
    if (!unpacker_->UnpackAsset(asset, config_verification_files[category])) {
      LOG4CXX_ERROR(loggerAppController,
                    "Failed to unpack config verification file with asset_id="
                        << static_cast<int>(asset));
    }

  std::string ep_dependencies_config_path;
  if (!unpacker_->UnpackAsset(Asset::kEPDependenciesConfig,
                              ep_dependencies_config_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack EP dependencies config!"
                      << ep_dependencies_config_path);
  }

  std::string ep_dependencies_config_schema_path;
  if (!unpacker_->UnpackAsset(Asset::kEPDependenciesConfigSchema,
                              ep_dependencies_config_schema_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack EP dependencies config schema!");
  }

  std::string vendors_default_zip_path;
  if (!unpacker_->UnpackAsset(Asset::kVendorsDefault, vendors_default_zip_path))
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack vendors default zip file!");

  auto configs_folder_path = unpacker_->GetDepsDir() + "/vendors_default";
  std::filesystem::remove_all(configs_folder_path);
  auto config_paths = unpacker_->UnpackFilesFromZIP(vendors_default_zip_path,
                                                    configs_folder_path, true);

  bool has_llama2_scenario = false;
  for (const auto& entry : config_paths) {
    auto config = std::make_shared<cil::ExecutionConfig>();
    bool json_loaded = config->ValidateAndParse(
        configs_folder_path + "/" + entry, config_schema_path_,
        config_verification_files);
    if (json_loaded) {
      const auto& scenarios = config->GetScenarios();
      if (!scenarios.empty() &&
          !scenarios.front().GetExecutionProviders().empty()) {
        const auto& scenario = scenarios.front();
        const auto& ep_config = scenario.GetExecutionProviders().front();
        std::string entry_file_name =
            std::filesystem::path(entry).stem().string();
        if (!cil::utils::IsEpConfigSupportedOnThisPlatform(entry_file_name) ||
            !cil::utils::IsEpSupportedOnThisPlatform("", ep_config.GetName()))
          continue;
        base_configs_.push_back(config);
        if (cil::utils::StringToLowerCase(scenario.GetName()) == "llama2")
          has_llama2_scenario = true;
      }
    } else {
      LOG4CXX_ERROR(loggerAppController,
                    "Unable to load config file " << entry);
    }
  }

  std::map<std::string, int> category_order = {
      {"", 0}, {"extended", 1}, {"experimental", 2}};
  std::stable_sort(base_configs_.begin(), base_configs_.end(),
                   [&](auto config1, auto config2) {
                     return category_order[config1->GetConfigCategory()] <
                            category_order[config2->GetConfigCategory()];
                   });

  std::string input_file_schema_path;
  if (has_llama2_scenario && !unpacker_->UnpackAsset(Asset::kLLMInputFileSchema,
                                                     input_file_schema_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack llama2 input file schema!");
  }
  std::string output_results_schema_path;
  if (!unpacker_->UnpackAsset(Asset::kOutputResultsSchema,
                              output_results_schema_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack output results schema!");
  }
  std::string data_verification_file_schema_path;
  if (!unpacker_->UnpackAsset(Asset::kDataVerificationFileSchema,
                              data_verification_file_schema_path)) {
    LOG4CXX_ERROR(loggerAppController,
                  "Failed to unpack data verification file schema!");
  }

  benchmark_controller_->SetRunnerConfigs(
      ep_dependencies_config_path, ep_dependencies_config_schema_path,
      input_file_schema_path, output_results_schema_path,
      data_verification_file_schema_path);
}

void AppController::InitStartPage() {
  unpacker_ = std::make_shared<cil::Unpacker>();
  benchmark_controller_->SetUnpacker(unpacker_);

  InitLogs();
  InitConfigs();

  QList<EPInformationCard> all_eps;
  for (const auto& config : base_configs_) {
    const auto& scenario = config->GetScenarios().front();
    const auto& ep_config = scenario.GetExecutionProviders().front();
    QString name = QString::fromStdString(ep_config.GetName());
    QString ep_display_name =
        QString::fromStdString(cil::EPNameToDisplayName(ep_config.GetName()));
    QString file_name =
        QString::fromStdString(config->GetConfigFileName()).split('.').first();
    QString card_name = gui::utils::EPCardName(file_name, name);
    all_eps.push_back({
        name,
        card_name,
        card_name.split(' ').back(),
        QString::fromStdString(config->GetSystemConfig().GetComment()),
        gui::utils::ModelDisplayName(scenario.GetName()),
        {},
        ep_config.GetConfig(),
        QString::fromStdString(config->GetConfigCategory()),
        "base",
        ep_display_name,
    });
  }

  start_page_controller_->LoadSystemInformation();

  emit SwitchToPage(PageType::kStartPage);
  emit ShowGlobalPopup("Downloading benchmark assets.\nPlease wait.", true);

  benchmark_controller_->SetConfigs(base_configs_);
  benchmark_controller_->SetDataDir(
      settings_page_controller_->GetDataPath().toStdString());
  benchmark_controller_->EnumerateDevices(all_eps);
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

  benchmark_controller_ = new BenchmarkController(
      realtime_monitoring_page_controller_, interrupt_, this);
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

  connect(benchmark_controller_,
          &BenchmarkController::EnumerationProgressChanged, this,
          &AppController::UpdateProgressPopup);
  connect(benchmark_controller_, &BenchmarkController::EnumerationFinished,
          this, &AppController::OnEnumerationFinished);
  connect(benchmark_controller_, &BenchmarkController::BenchmarkFinished, this,
          &AppController::OnBenchmarkFinished);
  connect(benchmark_controller_, &BenchmarkController::ClearCacheFinished, this,
          &AppController::OnClearCacheFinished);
  connect(benchmark_controller_,
          &BenchmarkController::DownloadDoNotAskAgainRequested,
          settings_page_controller_,
          &SettingsPageController::SetDoNotAskBeforeDownload);
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
  benchmark_controller_->SetOutputDir(output_dir_);
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

void AppController::OnEnumerationFinished() {
  auto schema = cil::ExecutionConfig::GetEPsConfigSchema(config_schema_path_);

  start_page_controller_->LoadEPsInformation(
      schema, benchmark_controller_->GetOverriddenEps());

  emit HidePopup();

  const auto& status = benchmark_controller_->GetBenchmarkStatus();
  if (!status.success_)
    emit ShowGlobalPopup(
        "Some assets preparation failed.\nRestarting the app may help\nresolve "
        "the issue.");
}

void AppController::OnBenchmarkFinished(bool download_deps_only) {
#ifdef Q_OS_IOS
  gui::ios_utils::SetIdleTimerDisabled(false);
#endif
  if (interrupt_) emit HidePopup();

  const auto& status = benchmark_controller_->GetBenchmarkStatus();

  // navigate back to the start page...
  emit SwitchToPage(PageType::kStartPage);
  start_page_controller_->ResetSelectedEPsConfigurations();

  results_history_page_controller_->LoadHistory(output_dir_);
  if (!download_deps_only && status.success_)
    emit SwitchToPage(PageType::kHistoryPage);

  QMetaObject::invokeMethod(
      realtime_monitoring_page_controller_,
      [=]() {
        realtime_monitoring_page_controller_->ShowStatus(
            download_deps_only ? "Download" : "Benchmark", status);
      },
      Qt::QueuedConnection);
}

void AppController::RunBenchmark(bool download_deps_only) {
  auto eps_configs = start_page_controller_->GetCurrentEPsConfigurations();
  configs_.clear();
  QList<EPInformationCard> eps;
  auto prepared_eps_configs = benchmark_controller_->GetPreparedEPsConfigs();
  eps.reserve(eps_configs.size());
  for (auto& ep_config : eps_configs) {
    auto config = std::make_shared<cil::ExecutionConfig>(
        *base_configs_.at(ep_config.first));
    auto device_it = ep_config.second.config_.find("Device");
    std::string device_str = device_it.value();
    auto device_index =
        ep_config.second.devices_.indexOf(QString::fromStdString(device_str));

    ep_config.second.config_.erase(device_it);

    if (device_index != -1) {
      ep_config.second.config_["device_id"] =
          prepared_eps_configs.at(ep_config.first)
              .at(device_index)["device_id"];
      ep_config.second.config_["device_name"] =
          prepared_eps_configs.at(ep_config.first)
              .at(device_index)["device_name"];
    }

    auto& scenario = config->GetScenarios().front();
    scenario.SetExecutionProviderConfig(0, ep_config.second.config_);
    scenario.SetAllowedInputType(ep_config.second.prompts_type_.toStdString());
    configs_.push_back(config);
    eps.append(ep_config.second);
  }
  realtime_monitoring_page_controller_->InitExecutionWithEPs(eps);
  benchmark_controller_->SetConfigs(configs_);
  benchmark_controller_->SetDataDir(
      settings_page_controller_->GetDataPath().toStdString());

#ifdef Q_OS_IOS
  gui::ios_utils::SetIdleTimerDisabled(true);
#endif

  benchmark_controller_->RunBenchmark(
      download_deps_only, settings_page_controller_->AskBeforeDownload());

  emit SwitchToPage(PageType::kRealTimeMonitoringPage);
}

void AppController::StopBenchmark(bool wait_for_finished) {
  benchmark_controller_->StopBenchmark(wait_for_finished);
}

void AppController::OnClearCacheRequested() {
  // make sure that the benchmark thread is not running
  StopBenchmark(true);

  emit ShowGlobalPopup("Clearing the cache. Please wait.");

  benchmark_controller_->SetDataDir(
      settings_page_controller_->GetDataPath().toStdString());
  benchmark_controller_->ClearCache();
}

void AppController::OnClearCacheFinished() { emit HidePopup(); }

}  // namespace controllers
}  // namespace gui