#include "results_history_page_controller.h"

#include "../CIL/benchmark/runner.h"
#include "../CIL/benchmark_logger.h"
#include "../CIL/utils.h"
#include "core/gui_utils.h"
#include "core/types.h"
#include "models/results_history_model.h"
#include "ui/results_history_page.h"

namespace gui {
namespace controllers {

ResultsHistoryPageController::ResultsHistoryPageController(QObject* parent)
    : AbstractController(parent), current_entry_(-1) {
  qRegisterMetaType<SystemInfoDetails>("SystemInfoDetails");
  InitializeModels();
}

void ResultsHistoryPageController::SetView(views::ResultsHistoryPage* view) {
  AbstractController::SetView(view);
  view->SetModel(sort_filter_model_);

  connect(view, &views::ResultsHistoryPage::OpenReportRequested, this,
          &ResultsHistoryPageController::OnOpenReportRequested);
  connect(view, &views::ResultsHistoryPage::DeleteHistoryRequested, this,
          &ResultsHistoryPageController::OnDeleteHistoryRequested);
  connect(view, &views::ResultsHistoryPage::SelectEntriesRequested, this,
          &ResultsHistoryPageController::UpdateAllRowsSelection);
  connect(view, &views::ResultsHistoryPage::SortRequested, this,
          &ResultsHistoryPageController::OnSortRequested);
  connect(view, &views::ResultsHistoryPage::DeviceTypeFilterRequested, this,
          [this](const QStringList& types) {
            sort_filter_model_->SetFilterDeviceTypes(types);
          });
}

void ResultsHistoryPageController::LoadHistory(
    const std::string& results_path) {
  if (!view_) {
    qDebug() << "ResultsHistoryPageController::LoadHistoryCards() view is null";
    return;
  }

  GetView()->SetModel(sort_filter_model_);

  results_file_path_ = results_path + "/results.json";
  auto results = cil::BenchmarkLogger::ReadResultsFromFile(results_file_path_);

  QList<HistoryEntry> entries;
  for (const auto& result : results) {
    QMap<QString, HistoryEntryPerfResult> perf_results;
    HistoryEntryPerfResult overall_entry;
    bool overall_entry_found = false;
    for (const auto& perf_result : result.performance_results) {
      HistoryEntryPerfResult entry_perf_result{
          perf_result.first.c_str(),
          perf_result.second.time_to_first_token_duration,
          perf_result.second.token_generation_rate};

      if (perf_result.first == "Overall") {
        overall_entry = entry_perf_result;
        overall_entry_found = true;
      } else {
        perf_results.insert(QString::fromStdString(perf_result.first),
                            entry_perf_result);
      }
    }

    bool contains_only_4k_8k = true;
    for (const auto& file_name : result.data_file_names)
      contains_only_4k_8k &=
          file_name.ends_with("4k.json") || file_name.ends_with("8k.json");

    QString datetime_str = QString::fromStdString(result.benchmark_start_time);
    if (overall_entry_found) perf_results.insert("Overall", overall_entry);
    QDateTime datetime =
        QDateTime::fromString(datetime_str, "yyyy-MM-dd hh:mm:ss ttt");
    if (!datetime.isValid()) {
      datetime_str.replace("..", ".");
      datetime =
          QDateTime::fromString(datetime_str, "MM-dd-yyyy hh:mm:ss.zzz ttt");
    }
    std::string config_file_name =
        std::filesystem::path(result.config_file_name).stem().string();
    const QString ep_name =
        QString::fromStdString(result.execution_provider_name);
    QString ep_display_name = gui::utils::EPCardName(
        QString::fromStdString(config_file_name), ep_name);

    auto model_full_name = cil::BenchmarkRunner::GetModelFullName(
        cil::utils::StringToLowerCase(result.scenario_name));

    auto model_display_name = model_full_name.has_value()
                                  ? model_full_name.value()
                                  : result.scenario_name;

    // The long prompts contains one or 2 prompts that end with 4k/8k
    bool support_long_prompts =
        ((perf_results.size() == 1 || perf_results.size() == 2) &&
         contains_only_4k_8k);

    HistoryEntry entry{model_display_name.c_str(),
                       ep_name,
                       ep_display_name,
                       result.device_type.c_str(),
                       datetime,
                       result.benchmark_success,
                       result.config_verified,
                       result.config_experimental,
                       perf_results,
                       result.error_message.c_str(),
                       support_long_prompts};
    if (!result.system_info.cpu_model.empty() &&
        !result.system_info.cpu_architecture.empty()) {
      entry.system_info_.cpu_name =
          QString("%1(%2)")
              .arg(QString::fromStdString(result.system_info.cpu_model))
              .arg(QString::fromStdString(result.system_info.cpu_architecture));
    }
    entry.system_info_.os_name =
        QString::fromStdString(result.system_info.os_name);
    entry.system_info_.ram =
        result.system_info.ram > 0
            ? gui::utils::BytesToGbString(result.system_info.ram)
            : "";
    entry.system_info_.gpu_name =
        QString::fromStdString(result.system_info.gpu_name);
    entry.system_info_.gpu_ram =
        result.system_info.gpu_ram > 0
            ? gui::utils::BytesToGbString(result.system_info.gpu_ram)
            : "";
    entries.append(entry);
  }

  model_->SetEntries(entries);
  GetView()->SetSortingMode("Newest first");
}

QList<HistoryEntry> ResultsHistoryPageController::GetCurrentEntries() const {
  QList<HistoryEntry> entries;
  // we consider all selected entries as current entries if there is no special
  // current entry assigned
  if (current_entry_ == -1) {
    auto selected_indexes = GetView()->GetSelectionModel()->selectedIndexes();
    for (auto& index : selected_indexes) {
      int row = sort_filter_model_->mapToSource(index).row();
      entries << model_->GetEntry(row);
    }
  } else {
    entries << model_->GetEntry(current_entry_);
  }
  return entries;
}

void ResultsHistoryPageController::OnDeleteHistoryRequested() {
  auto selected_indexes = GetView()->GetSelectionModel()->selectedIndexes();
  QVector<int> selected_rows;
  for (auto& index : selected_indexes)
    selected_rows.append(sort_filter_model_->mapToSource(index).row());
  std::sort(selected_rows.begin(), selected_rows.end(), std::greater<int>());
  for (auto row : selected_rows) model_->RemoveEntry(row);
  cil::BenchmarkLogger::RemoveResultsFromFile(
      results_file_path_,
      std::unordered_set<int>(selected_rows.begin(), selected_rows.end()));
}

void ResultsHistoryPageController::OnSortRequested(const QString& mode) {
  sort_filter_model_->SetSortingMode(mode);
  sort_filter_model_->sort(0);
}

void ResultsHistoryPageController::OnOpenReportRequested(int row) {
  if (row == -1) {
    current_entry_ = row;
  } else {
    if (row < 0 || row >= sort_filter_model_->rowCount()) return;
    current_entry_ =
        sort_filter_model_->mapToSource(sort_filter_model_->index(row, 0))
            .row();
  }
  emit OpenReportRequested();
}

void ResultsHistoryPageController::InitializeModels() {
  model_ = new models::ResultsHistoryModel(this);
  sort_filter_model_ = new models::ResultsHistoryProxyModel(this);
  sort_filter_model_->setSourceModel(model_);
  sort_filter_model_->setDynamicSortFilter(true);
}

views::ResultsHistoryPage* ResultsHistoryPageController::GetView() const {
  return dynamic_cast<gui::views::ResultsHistoryPage*>(view_);
}

void ResultsHistoryPageController::UpdateAllRowsSelection(int row,
                                                          bool select) {
  QItemSelection selection = QItemSelection(
      sort_filter_model_->index(row == -1 ? 0 : row, 0),
      sort_filter_model_->index(
          row == -1 ? sort_filter_model_->rowCount() - 1 : row, 0));
  GetView()->GetSelectionModel()->select(
      selection,
      select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
}

}  // namespace controllers
}  // namespace gui
