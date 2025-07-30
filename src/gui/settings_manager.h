#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <QSettings>
#include <QString>
#include <QVariant>

class SettingsManager {
 public:
  static SettingsManager& getInstance() {
    static SettingsManager instance;
    return instance;
  }

  void SetValue(const QString& key, const QVariant& value) {
    m_settings.setValue(key, value);
    m_settings.sync();
  }

  QVariant value(const QString& key,
                 const QVariant& defaultValue = QVariant()) const {
    return m_settings.value(key, defaultValue);
  }

  void EulaAccepted() { SetValue("eula_accepted", true); }
  bool IsEulaAccepted() const { return value("eula_accepted", false).toBool(); }

  void SetDataPath(const QString& path) { SetValue("data_path", path); }
  QString GetDataPath() const { return value("data_path", "").toString(); }

  void SetLogsPath(const QString& path) { SetValue("logs_path", path); }
  QString GetLogsPath() const { return value("logs_path", "").toString(); }

  void SetKeepLogs(bool keep) { SetValue("keep_logs", keep); }
  bool GetKeepLogs() const { return value("keep_logs", true).toBool(); }

 private:
  SettingsManager() : m_settings("MLPerf", "MLPerf GUI") {}
  SettingsManager(const SettingsManager&) = delete;
  SettingsManager& operator=(const SettingsManager&) = delete;

  QSettings m_settings;
};

#endif  // SETTINGS_MANAGER_H