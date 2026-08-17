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

#include <array>

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
 * @brief A single headline score (label + display-ready value) on a card.
 */
struct Metric {
  QString title;
  QString value;
};

// The two headline scores of a run; their meaning depends on the scenario
// (LLM: TTFT/TPS, Agentic: E2E/Tools, Image gen: Images-per-minute/Avg-E2E).
using MainScores = std::array<Metric, 2>;

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
  QString error_message_;
  QString config_file_comment_;
  MainScores main_scores_;

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
  QString scenario_kind_;     // "LLM", "Agentic", or "Image gen"
  QStringList prompt_types_;  // input groups the config declares
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

  // We need this as an id of the EP benchmark in the results
  QString benchmark_start_time_;
};

/**
 * @brief Overall benchmark status after execution, including both the global
 * action result and the status of each execution provider.
 */
struct BenchmarkStatus {
  bool success_ = false;
  bool download_accepted_ = false;
  // True when the run ended because the user cancelled it (declined the
  // download prompt or pressed Cancel)
  bool cancelled_ = false;
  bool size_info_collected_ = false;
  QString logs_path_;
  QList<EPBenchmarkStatus> eps_benchmark_status_;

  BenchmarkStatus() = default;
  explicit BenchmarkStatus(const QString& logs_path) : logs_path_(logs_path) {}
};
}  // namespace gui

Q_DECLARE_METATYPE(gui::SystemInfoDetails)
Q_DECLARE_METATYPE(gui::MainScores)

#endif  // __GUI_TYPES_H__