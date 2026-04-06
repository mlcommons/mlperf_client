#ifndef RESULTS_REPORT_PAGE_CONTROLLER_H_
#define RESULTS_REPORT_PAGE_CONTROLLER_H_

#include "../CIL/benchmark_result.h"
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

  void LoadResultsTable(
      const QList<QPair<HistoryEntry, cil::BenchmarkResult>>& entries);

 signals:
  void ReturnBackRequested();
  void RunNewBenchmarkRequested();

 private slots:
  void HandleExportReport();

 private:
  QList<HistoryEntry> table_entries_;
  std::vector<cil::BenchmarkResult> results_;

  void DoLoadResultsTable();

  views::ResultsReportPage* GetView() const;
};

}  // namespace controllers
}  // namespace gui

#endif  // RESULTS_REPORT_PAGE_CONTROLLER_H_