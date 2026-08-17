#include "results_report_page.h"

#include <QDateTime>
#include <QVBoxLayout>
#include <QWidget>
#ifdef Q_OS_IOS
#include <QScroller>
#endif

#include "widgets/report_io_views.h"
#include "widgets/result_table_widget.h"

namespace gui {
namespace views {
ResultsReportPage::ResultsReportPage(QWidget* parent)
    : AbstractView(parent), table_(nullptr) {}

void ResultsReportPage::SetupUi() {
  ui_.setupUi(this);
  ui_.results_button_->setProperty("class", "secondary_button_with_icon");
  ui_.title_label_->setProperty("class", "title");
  ui_.export_button_->setProperty("class", "secondary_button_with_icon");
  ui_.export_button_->setProperty("has_border", true);
  ui_.run_new_benchmark_button_->setProperty("class", "primary_button");
  ui_.TTFT_label_->setProperty("class", "medium_normal_label");
  ui_.TPS_label1_->setProperty("class", "medium_normal_label");
  ui_.TPS_label2_->setProperty("class", "medium_normal_label");
  ui_.ext_label1_->setProperty("class", "medium_normal_label");
  ui_.ext_label2_->setProperty("class", "medium_normal_label");

#ifdef Q_OS_IOS
  QScroller::grabGesture(ui_.result_table_frame_);
#endif
}

void ResultsReportPage::InstallSignalHandlers() {
  connect(ui_.results_button_, &QPushButton::clicked, this,
          &ResultsReportPage::BackButtonClicked);
  connect(ui_.run_new_benchmark_button_, &QPushButton::clicked, this,
          &ResultsReportPage::RunNewBenchmarkClicked);
  connect(ui_.export_button_, &QPushButton::clicked, this,
          &ResultsReportPage::ExportButtonClicked);
}

void ResultsReportPage::InitResultsTable(
    const QList<custom_widgets::HeaderInfo>& headers) {
  table_ = new custom_widgets::ResultTableWidget(headers);
  table_->setObjectName("result_table");

  ui_.result_table_frame_->setWidget(table_);
}

void ResultsReportPage::AddResultsTableTitle(const QString& title,
                                             const QString& sub_title) {
  if (!table_) return;
  table_->AddTitle(title, sub_title);
}

void ResultsReportPage::AddResultsTableRow(const QStringList& row_data,
                                           bool bold) {
  if (!table_) return;
  table_->AddBoldRow(row_data, bold);
}

custom_widgets::CategoryTitleToggle* ResultsReportPage::AddCategoryTitleRow(
    const QString& category) {
  if (!table_) return nullptr;
  auto* toggle = new custom_widgets::CategoryTitleToggle(category);
  table_->AppendCustomTitleRow(toggle);
  return toggle;
}

void ResultsReportPage::AddCategoryIORow(
    const QString& category, const std::vector<cil::BenchmarkResult>& results,
    custom_widgets::CategoryTitleToggle* toggle) {
  if (!table_) return;

  QList<custom_widgets::IOView*> views;
  // Merged layout: all entries share the same prompts → shared prompt
  // blocks span the full row width, only outputs differ per column.
  if (custom_widgets::ResultsShareInputs(results, category)) {
    auto* merged = new custom_widgets::MergedIOView(results, category);
    merged->setVisible(false);
    table_->AppendSpanningCustomRow(merged);
    views.append(merged);
  } else {
    // Per-entry layout: each column renders its own prompt/output stack.
    QList<QWidget*> cells;
    cells.append(nullptr);  // column 0 stays empty; toggle lives in title row.
    for (const auto& r : results) {
      auto* view = new custom_widgets::PerResultIOView(r, category);
      view->setVisible(false);
      views.append(view);
      cells.append(view);
    }
    table_->AppendCustomRow(cells);
  }

  if (toggle) {
    QObject::connect(toggle, &QPushButton::toggled, this,
                     [views](bool checked) {
                       for (auto* view : views) {
                         if (checked) view->EnsureBuilt();
                         view->setVisible(checked);
                       }
                     });
  }
}

}  // namespace views
}  // namespace gui
