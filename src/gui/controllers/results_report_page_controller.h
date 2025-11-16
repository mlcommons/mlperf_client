#ifndef RESULTS_REPORT_PAGE_CONTROLLER_H_
#define RESULTS_REPORT_PAGE_CONTROLLER_H_

#include "controllers/abstract_controller.h"

namespace gui {
struct HistoryEntry;

namespace views {
class ResultsReportPage;
}

namespace controllers {

/**
 * @class ResultsReportPageController
 * @brief Handles report generation and display for benchmark results.
 */
class ResultsReportPageController : public AbstractController {
  Q_OBJECT

 public:
  explicit ResultsReportPageController(QObject* parent = nullptr);

  void SetView(views::ResultsReportPage* view);

  QList<QString> GetOrderedIndexes(const QList<HistoryEntry>& entries) const;

  void LoadResultsTable(const QList<HistoryEntry>& entries);

 signals:
  void ReturnBackRequested();
  void RunNewBenchmarkRequested();

 private slots:
  void HandleExportReport();

 private:
  std::vector<QStringList> report_;
  QList<HistoryEntry> table_entries_;

  void DoLoadResultsTable();

  views::ResultsReportPage* GetView() const;
};

}  // namespace controllers
}  // namespace gui

#endif  // RESULTS_REPORT_PAGE_CONTROLLER_H_