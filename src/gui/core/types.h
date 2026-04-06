/**
 * @file types.h
 * @brief Core type definitions for the GUI module.
 *
 * Essential data structures and enums for pages, benchmark results, system
 * info, history entries, execution provider details, and filtering. Used for
 * data exchange across GUI components.
 */

#ifndef __GUI_TYPES_H__
#define __GUI_TYPES_H__

#include <QDateTime>
#include <QMap>
#include <nlohmann/json.hpp>

namespace gui {

/**
 * @brief Navigation pages in the application.
 */
enum class PageType {
  kEulaPage,
  kStartPage,
  kRealTimeMonitoringPage,
  kHistoryPage,
  kReportPage,
  kSettingsPage,
};

/**
 * @brief System hardware/software details for reproducibility.
 */
struct SystemInfoDetails {
  QString os_name;
  QString ram;
  QString cpu_name;
  QString gpu_name;
  QString gpu_ram;
};

/**
 * @brief Complete record of a benchmark execution.
 */
struct HistoryEntry {
  QString scenario_name_;
  QString ep_name_;
  QString ep_display_name_;
  QString device_type_;
  QDateTime date_time_;
  bool success_;
  bool tested_by_ml_commons_;
  QString config_category_;
  double overall_time_to_first_token_;
  double overall_token_generation_rate_;
  QString error_message_;
  QString config_file_comment_;

  SystemInfoDetails system_info_;
};

/**
 * @brief Information card for an EP.
 */
struct EPInformationCard {
  QString name_;
  QString long_name_;
  QString device_type_;
  QString description_;
  QString model_name_;
  QStringList devices_;
  nlohmann::json config_;
  QString config_category_;
  QString prompts_type_;
  QString mapped_name_;
};

/**
 * @brief Filter configuration for EPs.
 */
struct EPFilter {
  QString name;
  QList<QPair<QString, bool>> options;
};

/**
 * @brief Execution provider benchmark status after execution.
 */
struct EPBenchmarkStatus {
  QString ep_name_;
  bool success_;
  QString error_message_;
};

/**
 * @brief Overall benchmark status after execution, including both the global
 * action result and the status of each execution provider.
 */
struct BenchmarkStatus {
  bool success_;
  bool download_accepted_;
  bool size_info_collected_;
  QString logs_path_;
  QList<EPBenchmarkStatus> eps_benchmark_status_;
};
}  // namespace gui

Q_DECLARE_METATYPE(gui::SystemInfoDetails)

#endif  // __GUI_TYPES_H__