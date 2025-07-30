#include "results_report_page_controller.h"

#include <QFileDialog>

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
  connect(view, &views::ResultsReportPage::ShowSubTasksResultsToggled, this,
          &ResultsReportPageController::OnShowSubTasksResultsToggled);
}

QStringList ResultsReportPageController::GetOrderedIndexs(
    const QList<HistoryEntry>& entries, bool long_prompts_only) {
  QSet<QString> header_set;
  for (const HistoryEntry entry : entries) {
    if (long_prompts_only && !entry.support_long_prompts_) continue;
    for (const auto& key : entry.perf_results_map_.keys())
      header_set.insert(key);
  }
  QStringList sorted_headers = header_set.values();
  std::sort(sorted_headers.begin(), sorted_headers.end(),
            [](const QString& a, const QString& b) {
              if (a == "Overall" && b != "Overall") return true;
              if (b == "Overall" && a != "Overall") return false;
              return a < b;
            });
  return sorted_headers;
}

void ResultsReportPageController::LoadResultsTable(
    const QList<HistoryEntry>& entries) {
  GetView()->SetSubTasksChecked(true);
  table_entries_.clear();
  if (entries.empty()) return;
  table_entries_ = entries;
  DoLoadResultsTable();
}

void ResultsReportPageController::DoLoadResultsTable() {
  bool display_sub_tasks = GetView()->DisplaySubTasksEnabled();
  report_.clear();
  QList<custom_widgets::HeaderInfo> headers;
  QStringList ep_headers{"EP", "", ""};
  QStringList device_type_headers{"Device", "", ""};
  QStringList scenario_headers{"Scenario", "", ""};
  QStringList time_headers{"Date", "", ""};
  QStringList is_tested_headers{"Tested By MLCommons", "", ""};
  QStringList is_experimental_headers{"Is Experimental", "", ""};
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

    headers.push_back({entry.scenario_name_, entry.ep_name_,
                       ":/icons/resources/icons/bi_gpu_small-line.png",
                       entry.device_type_, entry.date_time_,
                       entry.tested_by_ml_commons_, entry.is_experimental,
                       std::move(sys_info_records)});
    ep_headers << entry.ep_name_;
    device_type_headers << entry.device_type_;
    scenario_headers << entry.scenario_name_;
    time_headers << entry.date_time_.toString("yyyy-MM-dd h:mm AP");
    is_tested_headers << QString(entry.tested_by_ml_commons_ ? "Yes" : "No");
    is_experimental_headers << QString(entry.is_experimental ? "Yes" : "No");
    QStringList parts;
    for (const auto& pair : sys_info_records) {
      parts << QString("%1: %2").arg(pair.first, pair.second);
    }
    system_info_headers << QString(parts.join(";"));
  }
  report_.push_back(ep_headers);
  report_.push_back(device_type_headers);
  report_.push_back(scenario_headers);
  report_.push_back(time_headers);
  report_.push_back(is_tested_headers);
  report_.push_back(is_experimental_headers);
  report_.push_back(system_info_headers);
  auto results_report_page =
      dynamic_cast<gui::views::ResultsReportPage*>(view_);
  results_report_page->InitResultsTable(headers);
  QStringList all_table_indexes = GetOrderedIndexs(table_entries_, false);
  QStringList long_prompts_indexes = GetOrderedIndexs(table_entries_, true);

  for (const QString& index_name : all_table_indexes) {
    bool support_long_prompts = long_prompts_indexes.contains(index_name);
    bool is_over_all_cell = (index_name == "Overall");
    if (!display_sub_tasks && !is_over_all_cell && !support_long_prompts)
      continue;
    QStringList TTFT_row;
    QStringList TPS_row{"", ""};
    results_report_page->AddResultsTableTitle("", index_name);
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
    results_report_page->AddResultsTableRow(
        TTFT_row_data, is_over_all_cell || support_long_prompts);
    results_report_page->AddResultsTableRow(
        TPS_row_data, is_over_all_cell || support_long_prompts);
    report_.push_back(TTFT_row);
    report_.push_back(TPS_row);
  }
}
void ResultsReportPageController::OnShowSubTasksResultsToggled(bool checked) {
  DoLoadResultsTable();
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
