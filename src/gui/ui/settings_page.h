#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include "abstract_view.h"
#include "ui_settings_page.h"

namespace gui {
namespace views {

class SettingsPage : public AbstractView {
  Q_OBJECT
 public:
  SettingsPage(QWidget* parent = nullptr);
  ~SettingsPage() = default;

  void SetDataPaths(const QStringList& paths);
  void SetLogPaths(const QStringList& paths);

  void SetDataCurrentPath(const QString& path);
  void SetLogsCurrentPath(const QString& path);
  void SetKeepLogs(bool keep);

  QString GetDataCurrentPath() const;
  QString GetLogsCurrentPath() const;
  bool GetKeepLogs() const;

 signals:
  void DataPathChanged(const QString& new_path);
  void LogsPathChanged(const QString& new_path);
  void KeepLogsChanged(bool keep);
  void ClearCacheRequested();
  void ResetToDefaultsRequested();

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 private:
  Ui::SettingsPage ui_;
};

}  // namespace views
}  // namespace gui

#endif  // SETTINGS_PAGE_H