#ifndef RESULTS_REPORT_PAGE_CONTROLLER_H_
#define RESULTS_REPORT_PAGE_CONTROLLER_H_

#include "controllers/abstract_controller.h"

namespace gui {
struct HistoryEntry;

namespace views {
class ResultsReportPage;
}

namespace controllers {

class ResultsReportPageController : public AbstractController {
  Q_OBJECT

 public:
  explicit ResultsReportPageController(QObject* parent = nullptr);
  void SetView(views::ResultsReportPage* view);
  QStringList GetOrderedIndexs(const QList<HistoryEntry>& entries,
                               bool long_prompts_only);
  void LoadResultsTable(const QList<HistoryEntry>& entries);

 signals:
  void ReturnBackRequested();
  void RunNewBenchmarkRequested();

 private slots:
  void HandleExportReport();
  void OnShowSubTasksResultsToggled(bool checked);

 private:
  std::vector<QStringList> report_;
  QList<HistoryEntry> table_entries_;
  void DoLoadResultsTable();
  views::ResultsReportPage* GetView() const;

};

}  // namespace controllers
}  // namespace gui

#endif  // RESULTS_REPORT_PAGE_CONTROLLER_H_