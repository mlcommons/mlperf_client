#include "results_report_page_controller.h"

#include <QFileDialog>
#include <algorithm>

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

namespace {

// TTFT or TPS for a performance-results category, or "" if absent.
QString LlmMetric(const cil::BenchmarkResult& r, const std::string& category,
                  bool ttft) {
  if (auto it = r.performance_results.find(category);
      it != r.performance_results.end()) {
    return QString::number(ttft ? it->second.time_to_first_token_duration
                                : it->second.token_generation_rate,
                           'f', ttft ? 2 : 1);
  }
  return QString();
}

// Whether the given category is an agentic scenario in any of the results.
bool CategoryIsAgentic(const std::vector<cil::BenchmarkResult>& results,
                       const std::string& category) {
  return std::any_of(results.begin(), results.end(), [&](const auto& r) {
    auto it = r.performance_results.find(category);
    return it != r.performance_results.end() && it->second.is_agentic;
  });
}

}  // namespace

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

  AddHeaders();
  AddOverallSection();
  AddCategorySections();
}

void ResultsReportPageController::AddHeaders() {
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
  GetView()->InitResultsTable(headers);
}

// Agentic keeps its own rows since it uses different prompts/turns and isn't
// folded into the LLM TTFT/TPS values.
void ResultsReportPageController::AddOverallSection() {
  /* const bool has_image =
      std::any_of(results_.begin(), results_.end(),
                  [](const auto& r) { return r.is_image_benchmark; }); */
  const bool has_llm_overall =
      std::any_of(results_.begin(), results_.end(), [](const auto& r) {
        return r.performance_results.find("Overall") !=
               r.performance_results.end();
      });
  /* const bool has_agentic =
      std::any_of(results_.begin(), results_.end(),
                  [](const auto& r) { return FindAgentic(r) != nullptr; }); */

  auto* view = GetView();
  if (has_llm_overall) {
    view->AddResultsTableTitle("", "Overall");

    QStringList ttft = {"TTFT (seconds)"}, tps = {"TPS"};
    for (const auto& r : results_) {
      ttft << LlmMetric(r, "Overall", true);
      tps << LlmMetric(r, "Overall", false);
    }
    view->AddResultsTableRow(ttft, true);
    view->AddResultsTableRow(tps, true);
  }

  /* if (has_image) AddImageMetricRows(true);

  if (has_agentic) {
    QStringList ttft = {"TTFT (Agentic, seconds)"}, tps = {"TPS (Agentic)"},
                dur = {"Expected Duration (seconds)"},
                tools = {"Tools Duration (seconds)"},
                ctx = {"Context Window (tokens)"};
    for (const auto& r : results_) {
      const cil::PerformanceResult* p = FindAgentic(r);
      ttft << (p ? QString::number(p->time_to_first_token_duration, 'f', 2)
                 : QString());
      tps << (p ? QString::number(p->token_generation_rate, 'f', 1)
                : QString());
      dur << (p ? QString::number(p->average_expected_duration, 'f', 3)
                : QString());
      tools << (p ? QString::number(p->average_tools_duration, 'f', 3)
                  : QString());
      ctx << (p ? QString::number(
                      static_cast<qulonglong>(p->average_input_tokens))
                : QString());
    }
    view->AddResultsTableRow(ttft, true);
    view->AddResultsTableRow(tps, true);
    view->AddResultsTableRow(dur, true);
    view->AddResultsTableRow(tools, true);
    view->AddResultsTableRow(ctx, false);
  } */
}

void ResultsReportPageController::AddCategorySections() {
  std::vector<std::string> llm_categories;
  for (const auto& c : cil::BenchmarkLogger::GetOrderedCategories(results_))
    if (c != "Overall") llm_categories.push_back(c);

  QStringList image_categories;
  for (const auto& r : results_) {
    if (!r.is_image_benchmark) continue;
    for (const auto& c : r.input_categories) {
      QString qc = QString::fromStdString(c);
      if (!qc.isEmpty() && !image_categories.contains(qc))
        image_categories << qc;
    }
  }

  auto* view = GetView();
  const auto& ext_categories = cil::BenchmarkResult::kLLMExtendedCategories;
  for (const auto& category : llm_categories) {
    bool is_extended = std::find(ext_categories.begin(), ext_categories.end(),
                                 category) != ext_categories.end();
    QString display = QString::fromStdString(category) +
                      (is_extended ? QStringLiteral(" †") : QString());
    auto* toggle = view->AddCategoryTitleRow(display);

    if (CategoryIsAgentic(results_, category)) {
      QStringList e2e = {"Avg Expected Duration (seconds)"},
                  tools = {"Avg Tools Duration (seconds)"},
                  ctx = {"Context Window (tokens)"};
      for (const auto& r : results_) {
        auto it = r.performance_results.find(category);
        const bool has = it != r.performance_results.end();
        e2e << (has ? QString::number(it->second.average_expected_duration, 'f',
                                      3)
                    : QString());
        tools << (has ? QString::number(it->second.average_tools_duration, 'f',
                                        3)
                      : QString());
        ctx << (has ? QString::number(static_cast<qulonglong>(
                          it->second.average_input_tokens))
                    : QString());
      }
      view->AddResultsTableRow(e2e, false);
      view->AddResultsTableRow(tools, false);
      view->AddResultsTableRow(ctx, false);
    } else {
      QStringList ttft = {"TTFT (seconds)"}, tps = {"TPS"};
      for (const auto& r : results_) {
        ttft << LlmMetric(r, category, true);
        tps << LlmMetric(r, category, false);
      }
      view->AddResultsTableRow(ttft, false);
      view->AddResultsTableRow(tps, false);
    }

    view->AddCategoryIORow(QString::fromStdString(category), results_, toggle);
  }

  for (const auto& cat : image_categories) {
    auto* toggle = view->AddCategoryTitleRow(cat);
    // Only aggregate image metrics exist today; reused for the single category.
    AddImageMetricRows(/*primary_bold=*/false);
    view->AddCategoryIORow(cat, results_, toggle);
  }
}

void ResultsReportPageController::AddImageMetricRows(bool primary_bold) {
  QStringList ipm = {"Images/Min"}, sps = {"Steps/Sec"},
              e2e = {"Avg E2E (seconds)"}, size = {"Image Size"},
              steps = {"Inference Steps"}, e2e_min = {"E2E Min (seconds)"},
              e2e_max = {"E2E Max (seconds)"},
              e2e_stdev = {"E2E Stdev (seconds)"},
              first_step = {"First-Step (seconds)"},
              setup_load = {"Setup Load (seconds)"},
              setup_warmup = {"Setup Warmup (seconds)"};
  for (const auto& r : results_) {
    bool img = r.is_image_benchmark;
    ipm << (img ? QString::number(r.images_per_minute, 'f', 2) : QString());
    sps << (img ? QString::number(r.steps_per_second, 'f', 2) : QString());
    e2e << (img ? QString::number(r.avg_end_to_end_seconds, 'f', 3)
                : QString());
    size << (img ? QString("%1x%2").arg(r.image_width).arg(r.image_height)
                 : QString());
    steps << (img ? QString::number(r.num_inference_steps) : QString());
    e2e_min << (img ? QString::number(r.min_end_to_end_seconds, 'f', 3)
                    : QString());
    e2e_max << (img ? QString::number(r.max_end_to_end_seconds, 'f', 3)
                    : QString());
    e2e_stdev << (img ? QString::number(r.stdev_end_to_end_seconds, 'f', 3)
                      : QString());
    first_step << (img ? QString::number(r.first_step_seconds, 'f', 3)
                       : QString());
    setup_load << (img ? QString::number(r.setup_load_seconds, 'f', 3)
                       : QString());
    setup_warmup << (img ? QString::number(r.setup_warmup_seconds, 'f', 3)
                         : QString());
  }
  auto* view = GetView();
  view->AddResultsTableRow(ipm, primary_bold);
  view->AddResultsTableRow(sps, primary_bold);
  view->AddResultsTableRow(e2e, false);
  view->AddResultsTableRow(size, false);
  view->AddResultsTableRow(steps, false);
  view->AddResultsTableRow(e2e_min, false);
  view->AddResultsTableRow(e2e_max, false);
  view->AddResultsTableRow(e2e_stdev, false);
  view->AddResultsTableRow(first_step, false);
  view->AddResultsTableRow(setup_load, false);
  view->AddResultsTableRow(setup_warmup, false);
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
