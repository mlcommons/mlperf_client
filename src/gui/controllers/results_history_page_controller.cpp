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

namespace {

// Formats a main score for the history card, showing "N/A" when unavailable.
QString FormatScore(double value, int precision) {
  return value == 0.0 ? QStringLiteral("N/A")
                      : QString::number(value, 'f', precision);
}

}  // namespace

ResultsHistoryPageController::ResultsHistoryPageController(QObject* parent)
    : AbstractController(parent), current_entry_(-1) {
  qRegisterMetaType<SystemInfoDetails>("SystemInfoDetails");
  qRegisterMetaType<MainScores>("MainScores");
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
  results_ = cil::BenchmarkLogger::ReadResultsFromFile(results_file_path_);

  QList<HistoryEntry> entries;
  for (const auto& result : results_) {
    QString datetime_str = QString::fromStdString(result.benchmark_start_time);
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

    auto model_display_name =
        gui::utils::ModelDisplayName(result.model_base_name);

    // The card's two main scores depend on the scenario type.
    const cil::PerformanceResult* agentic = nullptr;
    for (const auto& [category, perf] : result.performance_results)
      if (perf.is_agentic) {
        agentic = &perf;
        break;
      }

    MainScores main_scores;
    if (result.is_image_benchmark) {
      main_scores = {{{"Images/Min", FormatScore(result.images_per_minute, 2)},
                      {"Avg E2E (s)",
                       FormatScore(result.avg_end_to_end_seconds, 3)}}};
    } else if (agentic) {
      main_scores = {
          {{"E2E (s)", FormatScore(agentic->average_expected_duration, 3)},
           {"Tools (s)", FormatScore(agentic->average_tools_duration, 3)}}};
    } else {
      double overall_TTFT = 0.0;
      double overall_TPS = 0.0;
      if (auto overall_it = result.performance_results.find("Overall");
          overall_it != result.performance_results.end()) {
        overall_TTFT = overall_it->second.time_to_first_token_duration;
        overall_TPS = overall_it->second.token_generation_rate;
      }
      main_scores = {{{"TTFT", FormatScore(overall_TTFT, 2)},
                      {"TPS", FormatScore(overall_TPS, 1)}}};
    }

    HistoryEntry entry{model_display_name,
                       ep_name,
                       ep_display_name,
                       result.device_type.c_str(),
                       datetime,
                       result.benchmark_success,
                       result.config_verified,
                       result.config_category.c_str(),
                       QString::fromStdString(result.error_message).trimmed(),
                       result.config_file_comment.c_str()};
    entry.main_scores_ = main_scores;
    if (!result.system_info.cpu_model.empty() &&
        !result.system_info.cpu_architecture.empty()) {
      entry.system_info_.cpu_name = QString::fromStdString(
          cil::utils::FormatCPU(result.system_info.cpu_model,
                                result.system_info.cpu_architecture));
    }
    entry.system_info_.os_name =
        QString::fromStdString(result.system_info.os_name);

    entry.system_info_.ram = QString::fromStdString(
        cil::utils::FormatMemory(result.system_info.ram));

    entry.system_info_.gpu_name =
        QString::fromStdString(result.system_info.gpu_name);

    entry.system_info_.gpu_ram = QString::fromStdString(
        cil::utils::FormatMemory(result.system_info.gpu_ram));

    entries.append(entry);
  }

  model_->SetEntries(entries);
  GetView()->SetSortingMode("Newest first");
}

QList<QPair<HistoryEntry, cil::BenchmarkResult> >
ResultsHistoryPageController::GetCurrentEntries() const {
  QList<QPair<HistoryEntry, cil::BenchmarkResult> > entries;
  // we consider all selected entries as current entries if there is no special
  // current entry assigned
  if (current_entry_ == -1) {
    auto selected_indexes = GetView()->GetSelectionModel()->selectedIndexes();
    for (auto& index : selected_indexes) {
      int row = sort_filter_model_->mapToSource(index).row();
      entries << qMakePair(model_->GetEntry(row), results_.at(row));
    }
  } else {
    entries << qMakePair(model_->GetEntry(current_entry_),
                         results_.at(current_entry_));
  }
  return entries;
}

QList<QPair<HistoryEntry, cil::BenchmarkResult> >
ResultsHistoryPageController::GetEntriesByIds(const QStringList& ids) const {
  QList<QPair<HistoryEntry, cil::BenchmarkResult> > entries;
  for (const auto& id : ids)
    for (int i = 0; i < results_.size(); ++i)
      if (id == QString::fromStdString(results_.at(i).benchmark_start_time)) {
        entries << qMakePair(model_->GetEntry(i), results_.at(i));
        break;
      }
  return entries;
}

void ResultsHistoryPageController::OnDeleteHistoryRequested() {
  auto selected_indexes = GetView()->GetSelectionModel()->selectedIndexes();
  QVector<int> selected_rows;
  for (auto& index : selected_indexes)
    selected_rows.append(sort_filter_model_->mapToSource(index).row());
  std::sort(selected_rows.begin(), selected_rows.end(), std::greater<int>());
  for (auto row : selected_rows) {
    model_->RemoveEntry(row);
    results_.erase(results_.begin() + row);
  }
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
