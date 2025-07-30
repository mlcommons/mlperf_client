#include "results_report_page.h"

#include <QDateTime>
#ifdef Q_OS_IOS
#include <QScroller>
#endif

#include "widgets/result_table_widget.h"

namespace gui {
namespace views {
ResultsReportPage::ResultsReportPage(QWidget* parent)
    : AbstractView(parent), table_(nullptr) {}

void ResultsReportPage::SetupUi() {
  ui_.setupUi(this);
  m_show_sub_tasks_result_button_ = new ToggleButton();
  ui_.top_rh_layout_->addWidget(m_show_sub_tasks_result_button_);

  ui_.results_button_->setProperty("class", "secondary_button_with_icon");
  ui_.title_label_->setProperty("class", "title");
  ui_.export_button_->setProperty("class", "secondary_button_with_icon");
  ui_.export_button_->setProperty("has_border", true);
#ifdef Q_OS_IOS
  ui_.export_button_->setEnabled(false);
#endif
  ui_.run_new_benchmark_button_->setProperty("class", "primary_button");
  ui_.TTFT_label_->setProperty("class", "medium_normal_label");
  ui_.TPS_label1_->setProperty("class", "medium_normal_label");
  ui_.TPS_label2_->setProperty("class", "medium_normal_label");

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
  connect(m_show_sub_tasks_result_button_, &ToggleButton::toggled, this,
          &ResultsReportPage::ShowSubTasksResultsToggled);
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

}  // namespace views
}  // namespace gui
