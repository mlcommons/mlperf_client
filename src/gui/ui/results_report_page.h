#ifndef RESULTS_REPORT_PAGE_H_
#define RESULTS_REPORT_PAGE_H_

#include "abstract_view.h"
#include "ui_results_report_page.h"
#include "widgets/toggle_button.h"

namespace custom_widgets {
struct HeaderInfo;
class ResultTableWidget;
}  // namespace custom_widgets

namespace gui {
namespace views {

class ResultsReportPage : public AbstractView {
  Q_OBJECT
 public:
  explicit ResultsReportPage(QWidget* parent = nullptr);
  ~ResultsReportPage() = default;

  void InitResultsTable(const QList<custom_widgets::HeaderInfo>& headers);
  void AddResultsTableTitle(const QString& title, const QString& sub_title);
  void AddResultsTableRow(const QStringList& row_data, bool bold);
  bool DisplaySubTasksEnabled() {
    return m_show_sub_tasks_result_button_->isChecked();
  }
  void SetSubTasksChecked(bool checked) {
    m_show_sub_tasks_result_button_->setChecked(checked);
  }

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 signals:
  void BackButtonClicked();
  void RunNewBenchmarkClicked();
  void ExportButtonClicked();
  void ShowSubTasksResultsToggled(bool checked);

 private:
  Ui::ResultsReportPage ui_;
  ToggleButton* m_show_sub_tasks_result_button_;
  custom_widgets::ResultTableWidget* table_;
};

}  // namespace views
}  // namespace gui

#endif  // RESULTS_REPORT_PAGE_H_