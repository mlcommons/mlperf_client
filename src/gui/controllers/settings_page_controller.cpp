#include "settings_page_controller.h"

#include <QStandardPaths>

#include "settings_manager.h"
#include "ui/settings_page.h"
#include "utils.h"

namespace gui {
namespace controllers {
SettingsPageController::SettingsPageController(const QString& data_default_path,
                                               const QString& logs_default_path,
                                               QObject* parent)
    : AbstractController(parent),
      data_default_path_(data_default_path),
      logs_default_path_(logs_default_path) {
  data_default_path_.replace("\\", "/");
  logs_default_path_.replace("\\", "/");
}
void SettingsPageController::SetView(views::SettingsPage* view) {
  AbstractController::SetView(view);

#ifdef Q_OS_IOS
  view->SetDataPaths({data_default_path_});
  view->SetLogPaths({logs_default_path_});
#else
  QString mlperf_app_data_path =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      "/MLPerf";
  QString mlperf_documents_path =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
      "/MLPerf";
  QStringList data_paths = {mlperf_app_data_path + "/data",
                            mlperf_documents_path + "/data"};
  QStringList log_paths = {mlperf_app_data_path, mlperf_documents_path};
  data_default_path_ = data_paths.front();
  logs_default_path_ = log_paths.front();

  view->SetDataPaths(data_paths);
  view->SetLogPaths(log_paths);
#endif
  view->SetKeepLogs(SettingsManager::getInstance().GetKeepLogs());

  const auto& settings = SettingsManager::getInstance();
  SetDataCurrentPath(settings.GetDataPath());
  SetLogsCurrentPath(settings.GetLogsPath());

  connect(view, &views::SettingsPage::DataPathChanged, this,
          &SettingsPageController::OnDataPathChanged);
  connect(view, &views::SettingsPage::LogsPathChanged, this,
          &SettingsPageController::OnLogsPathChanged);
  connect(view, &views::SettingsPage::KeepLogsChanged, this,
          &SettingsPageController::OnKeepLogsChanged);
  connect(view, &views::SettingsPage::ClearCacheRequested, this,
          &SettingsPageController::ClearCacheRequested);
  connect(view, &views::SettingsPage::ResetToDefaultsRequested, this,
          &SettingsPageController::OnResetToDefaultsRequested);
}

QString SettingsPageController::GetDataPath() const {
  if (view_) {
    auto settings_page = dynamic_cast<views::SettingsPage*>(view_);
    return settings_page->GetDataCurrentPath();
  }
  return QString();
}

QString SettingsPageController::GetLogsPath() const {
  if (view_) {
    auto settings_page = dynamic_cast<views::SettingsPage*>(view_);
    return settings_page->GetLogsCurrentPath();
  }
  return QString();
}

bool SettingsPageController::GetKeepLogs() const {
  if (view_) {
    auto settings_page = dynamic_cast<views::SettingsPage*>(view_);
    return settings_page->GetKeepLogs();
  }
  return true;
}

void SettingsPageController::OnDataPathChanged(const QString& path) {
  SettingsManager::getInstance().SetDataPath(path);
  emit DataPathChanged(path);
}

void SettingsPageController::OnLogsPathChanged(const QString& path) {
  SettingsManager::getInstance().SetLogsPath(path);
  emit LogsPathChanged(path);
}

void SettingsPageController::OnKeepLogsChanged(bool checked) {
  SettingsManager::getInstance().SetKeepLogs(checked);
  emit KeepLogsChanged(checked);
}

void SettingsPageController::OnResetToDefaultsRequested() {
  if (view_) {
    auto settings_page = dynamic_cast<views::SettingsPage*>(view_);
    settings_page->SetDataCurrentPath(data_default_path_);
    OnDataPathChanged(data_default_path_);

    OnKeepLogsChanged(true);
    settings_page->SetLogsCurrentPath(logs_default_path_);
    OnLogsPathChanged(logs_default_path_);
  }
}

void SettingsPageController::SetDataCurrentPath(const QString& path) {
  auto settings_page = dynamic_cast<views::SettingsPage*>(view_);
  if (cil::utils::IsDirectoryWritable(path.toStdString())) {
    settings_page->SetDataCurrentPath(path);
    return;
  }
  settings_page->SetDataCurrentPath(data_default_path_);
}

void SettingsPageController::SetLogsCurrentPath(const QString& path) {
  auto settings_page = dynamic_cast<views::SettingsPage*>(view_);
  if (cil::utils::IsDirectoryWritable(path.toStdString())) {
    settings_page->SetLogsCurrentPath(path);
    return;
  }
  settings_page->SetLogsCurrentPath(logs_default_path_);
}

}  // namespace controllers
}  // namespace gui
