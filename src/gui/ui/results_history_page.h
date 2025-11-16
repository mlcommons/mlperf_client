#ifndef RESULTS_HISTORY_PAGE_H_
#define RESULTS_HISTORY_PAGE_H_

#include "abstract_view.h"
#include "ui_results_history_page.h"

class QAbstractItemModel;
class QItemSelectionModel;

namespace gui {
namespace views {

/**
 * @class ResultsHistoryPage
 * @brief Page for displaying and managing benchmark results history.
 */
class ResultsHistoryPage : public AbstractView {
  Q_OBJECT

 public:
  explicit ResultsHistoryPage(QWidget* parent = nullptr);
  /**
   * @brief Set the data model for results display.
   */
  void SetModel(QAbstractItemModel* model);

  /**
   * @brief Access selection model for results list.
   */
  QItemSelectionModel* GetSelectionModel() const;

  /**
   * @brief Set the sorting mode for the results.
   */
  void SetSortingMode(const QString& mode);

 signals:
  void OpenReportRequested(int row);
  void DeleteHistoryRequested();
  void SelectEntriesRequested(int row, bool select);
  void SortRequested(const QString& mode);
  void DeviceTypeFilterRequested(const QStringList& types);

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 private slots:
  void OnRowsInserted(const QModelIndex& parent, int first, int last);
  void OnRowSelectionChanged(const QItemSelection& selected,
                             const QItemSelection& deselected);
  void OnRowOpenButtonClicked();
  void OnRowSelectionBoxChecked(bool checked);
  void OnDeleteButtonClicked();

 private:
  void UpdateButtons();

  Ui::ResultsHistoryPage ui_;  ///< UI form generated from the .ui file
};

}  // namespace views
}  // namespace gui

#endif  // RESULTS_HISTORY_PAGE_H_