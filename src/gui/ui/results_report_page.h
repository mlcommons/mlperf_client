#ifndef RESULTS_REPORT_PAGE_H_
#define RESULTS_REPORT_PAGE_H_

#include <vector>

#include "abstract_view.h"
#include "ui_results_report_page.h"
#include "widgets/toggle_button.h"

namespace cil {
struct BenchmarkResult;
}

namespace custom_widgets {
struct HeaderInfo;
class ResultTableWidget;
class CategoryTitleToggle;
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

  /**
   * @brief Begin a category section with a clickable title row whose
   *        chevron toggles the per-category I/O view added later via
   *        AddCategoryIORow. Returns the toggle widget so the caller can
   *        pass it through.
   */
  custom_widgets::CategoryTitleToggle* AddCategoryTitleRow(
      const QString& category);

  /**
   * @brief Append an I/O row to the metrics table for the given category.
   *        Column 0 stays empty; columns 1..N each hold a lazy per-entry
   *        prompt/output view that materialises on first expansion. The
   *        provided @p toggle (returned from AddCategoryTitleRow) drives
   *        the visibility.
   */
  void AddCategoryIORow(const QString& category,
                        const std::vector<cil::BenchmarkResult>& results,
                        custom_widgets::CategoryTitleToggle* toggle);

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