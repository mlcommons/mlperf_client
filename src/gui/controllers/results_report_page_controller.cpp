#include "results_report_page_controller.h"

#include <QFileDialog>

#include "../CIL/benchmark_logger.h"
#include "core/types.h"
#include "ui/results_report_page.h"
#include "ui/widgets/popup_widget.h"
#include "ui/widgets/result_table_widget.h"
#ifdef Q_OS_IOS
#include <QStandardPaths>

#include "ios/ios_utils.h"
#endif
namespace gui {
namespace controllers {

ResultsReportPageController::ResultsReportPageController(QObject* parent)
    : AbstractController(parent) {}

void ResultsReportPageController::SetView(views::ResultsReportPage* view) {
  AbstractController::SetView(view);
  connect(view, &views::ResultsReportPage::BackButtonClicked, this,
          &ResultsReportPageController::ReturnBackRequested);
  connect(view, &views::ResultsReportPage::RunNewBenchmarkClicked, this,
          &ResultsReportPageController::RunNewBenchmarkRequested);
  connect(view, &views::ResultsReportPage::ExportButtonClicked, this,
          &ResultsReportPageController::HandleExportReport);
}

void ResultsReportPageController::LoadResultsTable(
    const QList<QPair<HistoryEntry, cil::BenchmarkResult>>& entries) {
  table_entries_.clear();
  results_.clear();
  if (entries.empty()) return;
  table_entries_.reserve(entries.size());
  results_.reserve(entries.size());
  for (const auto& entry : entries) {
    table_entries_.push_back(entry.first);
    results_.push_back(entry.second);
  }
  DoLoadResultsTable();
}

void ResultsReportPageController::DoLoadResultsTable() {
  QList<custom_widgets::HeaderInfo> headers;
  for (const auto& entry : table_entries_) {
    QList<QPair<QString, QString>> sys_info_records = {
        {"CPU", entry.system_info_.cpu_name},
        {"RAM", entry.system_info_.ram},
        {"GPU", entry.system_info_.gpu_name},
        {"VRAM", entry.system_info_.gpu_ram},
        {"OS", entry.system_info_.os_name},
    };
    sys_info_records.erase(
        std::remove_if(sys_info_records.begin(), sys_info_records.end(),
                       [](const QPair<QString, QString>& pair) {
                         return pair.second.trimmed().isEmpty();
                       }),
        sys_info_records.end());

    headers.push_back(
        {entry.scenario_name_, entry.ep_name_,
         ":/icons/resources/icons/bi_gpu_small-line.png", entry.device_type_,
         entry.date_time_, entry.tested_by_ml_commons_, entry.config_category_,
         std::move(sys_info_records), entry.config_file_comment_});
  }
  auto results_report_page =
      dynamic_cast<gui::views::ResultsReportPage*>(view_);
  results_report_page->InitResultsTable(headers);
  auto categories = cil::BenchmarkLogger::GetOrderedCategories(results_);

  const auto& ext_categories = cil::BenchmarkResult::kLLMExtendedCategories;

  for (const auto& category : categories) {
    bool is_over_all_cell = (category == "Overall");
    bool is_extended = std::find(ext_categories.begin(), ext_categories.end(),
                                 category) != ext_categories.end();
    results_report_page->AddResultsTableTitle(
        "", QString::fromStdString(category +
                                   (is_extended ? "<sup>&#8224;</sup>" : "")));
    QStringList TTFT_row_data = {"TTFT (seconds)"};
    QStringList TPS_row_data = {"TPS"};
    for (const auto& r : results_) {
      if (auto it = r.performance_results.find(category);
          it != r.performance_results.end()) {
        TTFT_row_data << QString::number(
            it->second.time_to_first_token_duration, 'f', 2);
        TPS_row_data << QString::number(it->second.token_generation_rate, 'f',
                                        1);
      } else {
        TTFT_row_data << "";
        TPS_row_data << "";
      }
    }
    results_report_page->AddResultsTableRow(TTFT_row_data, is_over_all_cell);
    results_report_page->AddResultsTableRow(TPS_row_data, is_over_all_cell);
  }
}

void ResultsReportPageController::HandleExportReport() {
#ifdef Q_OS_IOS
  QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  QDir().mkpath(dir);  // ensure exists
  QString file_name = dir + "/report.csv";
#else
  QString file_name = QFileDialog::getSaveFileName(nullptr, "Save As", "report",
                                                   "CSV Files (*.csv)");
#endif

  cil::BenchmarkLogger::ExportResultsToCSV(results_, file_name.toStdString());
#ifdef Q_OS_IOS
  bool ok = gui::ios_utils::ShareFile(QUrl::fromLocalFile(file_name));
  if (!ok) PopupWidget::ShowMessage("Unable to open share sheet.", view_);
#else
  PopupWidget::ShowMessage("The report has been saved.", view_);
#endif
}

views::ResultsReportPage* ResultsReportPageController::GetView() const {
  auto report_page = dynamic_cast<views::ResultsReportPage*>(view_);
  return report_page;
}

}  // namespace controllers
}  // namespace gui
