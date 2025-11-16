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

/**
 * @class ResultsReportPage
 * @brief Page for displaying detailed benchmark performance reports.
 */
class ResultsReportPage : public AbstractView {
  Q_OBJECT

 public:
  explicit ResultsReportPage(QWidget* parent = nullptr);
  ~ResultsReportPage() = default;

  /**
   * @brief Initialize the results table with specified headers.
   */
  void InitResultsTable(const QList<custom_widgets::HeaderInfo>& headers);

  /**
   * @brief Add title and subtitle to the results table.
   */
  void AddResultsTableTitle(const QString& title, const QString& sub_title);

  /**
   * @brief Add a data row to the results table.
   */
  void AddResultsTableRow(const QStringList& row_data, bool bold);
 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 signals:
  void BackButtonClicked();
  void RunNewBenchmarkClicked();
  void ExportButtonClicked();

 private:
  Ui::ResultsReportPage ui_;
  custom_widgets::ResultTableWidget* table_;
};

}  // namespace views
}  // namespace gui

#endif  // RESULTS_REPORT_PAGE_H_