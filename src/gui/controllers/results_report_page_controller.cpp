#include "results_report_page_controller.h"

#include <QFileDialog>

#include "../CIL/benchmark_result.h"
#include "core/types.h"
#include "ui/results_report_page.h"
#include "ui/widgets/popup_widget.h"
#include "ui/widgets/result_table_widget.h"
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

QList<QString> ResultsReportPageController::GetOrderedIndexes(
    const QList<HistoryEntry>& entries) const {
  const auto& required_categories =
      cil::BenchmarkResult::kLLMRequiredCategories;
  const auto& ext_categories = cil::BenchmarkResult::kLLMExtendedCategories;

  QMap<QString, int> order;
  order["Overall"] = -1;  // smallest value in the order map
  for (int i = 0; i < required_categories.size(); ++i)
    order[QString::fromStdString(required_categories[i])] = i;
  for (int i = 0; i < ext_categories.size(); ++i)
    order[QString::fromStdString(ext_categories[i])] =
        required_categories.size() + i;

  QList<QString> sorted_headers;

  QSet<QString> header_set;
  for (const HistoryEntry entry : entries)
    for (const auto& key : entry.perf_results_map_.keys())
      header_set.insert(key);

  for (const auto& key : header_set) sorted_headers << key;

  std::sort(sorted_headers.begin(), sorted_headers.end(),
            [&](const QString& a, const QString& b) {
              auto it_a = order.find(a);
              auto it_b = order.find(b);
              if (it_a != order.end() && it_b != order.end())
                return it_a.value() < it_b.value();

              if (it_a != order.end()) return true;
              if (it_b != order.end()) return false;

              return a < b;
            });
  return sorted_headers;
}

void ResultsReportPageController::LoadResultsTable(
    const QList<HistoryEntry>& entries) {
  table_entries_.clear();
  if (entries.empty()) return;
  table_entries_ = entries;
  DoLoadResultsTable();
}

void ResultsReportPageController::DoLoadResultsTable() {
  report_.clear();
  QList<custom_widgets::HeaderInfo> headers;
  QStringList ep_headers{"EP", "", ""};
  QStringList device_type_headers{"Device", "", ""};
  QStringList scenario_headers{"Scenario", "", ""};
  QStringList time_headers{"Date", "", ""};
  QStringList is_tested_headers{"Tested By MLCommons", "", ""};
  QStringList config_category_headers{"Config Category", "", ""};
  QStringList system_info_headers{"System Info", "", ""};
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

    QStringList parts;
    for (const auto& pair : sys_info_records) {
      parts << QString("%1: %2").arg(pair.first, pair.second);
    }
    system_info_headers << QString(parts.join(";"));

    headers.push_back(
        {entry.scenario_name_, entry.ep_name_,
         ":/icons/resources/icons/bi_gpu_small-line.png", entry.device_type_,
         entry.date_time_, entry.tested_by_ml_commons_, entry.config_category_,
         std::move(sys_info_records), entry.config_file_comment_});
    ep_headers << entry.ep_name_;
    device_type_headers << entry.device_type_;
    scenario_headers << entry.scenario_name_;
    time_headers << entry.date_time_.toString("yyyy-MM-dd h:mm AP");
    is_tested_headers << QString(entry.tested_by_ml_commons_ ? "Yes" : "No");
    config_category_headers
        << (entry.config_category_.isEmpty() ? "base" : entry.config_category_);
  }
  report_.push_back(ep_headers);
  report_.push_back(device_type_headers);
  report_.push_back(scenario_headers);
  report_.push_back(time_headers);
  report_.push_back(is_tested_headers);
  report_.push_back(config_category_headers);
  report_.push_back(system_info_headers);
  auto results_report_page =
      dynamic_cast<gui::views::ResultsReportPage*>(view_);
  results_report_page->InitResultsTable(headers);
  auto all_table_indexes = GetOrderedIndexes(table_entries_);

  const auto& ext_categories = cil::BenchmarkResult::kLLMExtendedCategories;

  for (const auto& index_name : all_table_indexes) {
    bool is_over_all_cell = (index_name == "Overall");
    QStringList TTFT_row{"", ""};
    QStringList TPS_row{"", ""};
    bool is_extended = std::find(ext_categories.begin(), ext_categories.end(),
                                 index_name) != ext_categories.end();
    results_report_page->AddResultsTableTitle(
        "", index_name + (is_extended ? "<sup>&#8224;</sup>" : ""));
    QStringList TTFT_row_data = {"TTFT (seconds)"};
    QStringList TPS_row_data = {"TPS"};
    for (const auto& entry : table_entries_) {
      if (auto it = entry.perf_results_map_.find(index_name);
          it != entry.perf_results_map_.end()) {
        TTFT_row_data << QString::number(it.value().time_to_first_token_, 'f',
                                         2);
        TPS_row_data << QString::number(it.value().token_generation_rate_, 'f',
                                        1);
      } else {
        TTFT_row_data << "";
        TPS_row_data << "";
      }
    }
    TTFT_row += TTFT_row_data;
    TPS_row += TPS_row_data;
    results_report_page->AddResultsTableRow(TTFT_row_data, is_over_all_cell);
    results_report_page->AddResultsTableRow(TPS_row_data, is_over_all_cell);
    report_.push_back({index_name});
    report_.push_back(TTFT_row);
    report_.push_back(TPS_row);
  }
}

void ResultsReportPageController::HandleExportReport() {
  QString file_name = QFileDialog::getSaveFileName(nullptr, "Save As", "report",
                                                   "CSV Files (*.csv)");
  if (file_name.isEmpty()) return;
  QFile csv_file(file_name);
  if (csv_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&csv_file);
    auto escapeCSVField = [](const QString& field) -> QString {
      if (field.contains(',') || field.contains('"') || field.contains('\n')) {
        return "\"" + field + "\"";
      }
      return field;
    };
    for (const QStringList& row : report_) {
      QStringList cleaned_row;
      for (const QString& field : row) {
        cleaned_row << escapeCSVField(field);
      }
      out << cleaned_row.join(",") << "\n";
    }
    csv_file.close();
    PopupWidget::ShowMessage("The report has been saved.", view_);

  } else {
    PopupWidget::ShowMessage("Unable to open file for writing.", view_);
  }
}

views::ResultsReportPage* ResultsReportPageController::GetView() const {
  auto report_page = dynamic_cast<views::ResultsReportPage*>(view_);
  return report_page;
}

}  // namespace controllers
}  // namespace gui
