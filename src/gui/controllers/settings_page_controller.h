#ifndef SETTINGS_PAGE_CONTROLLER_H_
#define SETTINGS_PAGE_CONTROLLER_H_

#include "controllers/abstract_controller.h"

namespace gui {
namespace views {
class SettingsPage;
}
namespace controllers {
class SettingsPageController : public AbstractController {
  Q_OBJECT
 public:
  explicit SettingsPageController(const QString& data_default_path,
                                  const QString& logs_default_path,
                                  QObject* parent = nullptr);
  void SetView(views::SettingsPage* view);
  QString GetDataPath() const;
  QString GetLogsPath() const;
  bool GetKeepLogs() const;

 signals:
  void DataPathChanged(const QString& path);
  void LogsPathChanged(const QString& path);
  void ClearCacheRequested();
  void KeepLogsChanged(bool keep);

 private slots:
  void OnDataPathChanged(const QString& path);
  void OnLogsPathChanged(const QString& path);
  void OnKeepLogsChanged(bool keep);
  void OnResetToDefaultsRequested();

 private:
  void SetDataCurrentPath(const QString& path);
  void SetLogsCurrentPath(const QString& path);

  QString data_default_path_;
  QString logs_default_path_;
};
}  // namespace controllers
}  // namespace gui

#endif  // START_PAGE_CONTROLLER_H_