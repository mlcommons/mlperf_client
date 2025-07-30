#ifndef RESULTS_HISTORY_PAGE_CONTROLLER_H_
#define RESULTS_HISTORY_PAGE_CONTROLLER_H_

#include "../CIL/benchmark_result.h"
#include "controllers/abstract_controller.h"

namespace gui {
struct HistoryEntry;

namespace views {
class ResultsHistoryPage;
}

namespace models {
class ResultsHistoryModel;
class ResultsHistoryProxyModel;
}  // namespace models

namespace controllers {

class ResultsHistoryPageController : public AbstractController {
  Q_OBJECT

 public:
  explicit ResultsHistoryPageController(QObject* parent = nullptr);
  void SetView(views::ResultsHistoryPage* view);
  void LoadHistory(const std::string& results_path);
  QList<HistoryEntry> GetCurrentEntries() const;

 public slots:
  void OnDeleteHistoryRequested();
  void OnSortRequested(const QString& mode);
 signals:
  void OpenReportRequested();

 private slots:
  void OnOpenReportRequested(int row);

 private:
  void InitializeModels();
  views::ResultsHistoryPage* GetView() const;
  void UpdateAllRowsSelection(int row, bool select);

  models::ResultsHistoryModel* model_;
  models::ResultsHistoryProxyModel* sort_filter_model_;
  
  std::string results_file_path_;
  int current_entry_;
};

}  // namespace controllers
}  // namespace gui

#endif  // RESULTS_HISTORY_PAGE_CONTROLLER_H_