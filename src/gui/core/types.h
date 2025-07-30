#ifndef __GUI_TYPES_H__
#define __GUI_TYPES_H__

#include <QDateTime>
#include <QMap>
#include <nlohmann/json.hpp>

namespace gui {
enum class PageType {
  kEulaPage,
  kStartPage,
  kRealTimeMonitoringPage,
  kHistoryPage,
  kReportPage,
  kSettingsPage,
};

struct HistoryEntryPerfResult {
  QString name;
  double time_to_first_token_;
  double token_generation_rate_;
};

struct SystemInfoDetails {
  QString os_name;
  QString ram;
  QString cpu_name;
  QString gpu_name;
  QString gpu_ram;
};

struct HistoryEntry {
  QString scenario_name_;
  QString ep_name_;
  QString ep_display_name_;
  QString device_type_;
  QDateTime date_time_;
  bool success_;
  bool tested_by_ml_commons_;
  bool is_experimental;
  QMap<QString, HistoryEntryPerfResult> perf_results_map_;
  QString error_message_;
  bool support_long_prompts_;

  SystemInfoDetails system_info_;
};

struct EPInformationCard {
  QString name_;
  QString long_name_;
  QString device_type_;
  QString description_;
  QString model_name_;
  QStringList devices_;
  nlohmann::json config_;
  bool is_experimental;
  bool support_long_prompts;
  QString mapped_name_;
};

struct EPFilter {
  QString name;
  std::map<QString, bool> options;
};

}  // namespace gui

Q_DECLARE_METATYPE(gui::SystemInfoDetails)

#endif  // __GUI_TYPES_H__