#ifndef RESULTS_HISTORY_ENTRY_WIDGET_H_
#define RESULTS_HISTORY_ENTRY_WIDGET_H_

#include "core/types.h"
#include "ui_results_history_entry_widget.h"

class ResultsHistoryEntryWidget : public QWidget {
  Q_OBJECT
 public:
  ResultsHistoryEntryWidget(const QString &name, const QDateTime &dateTime,
                            bool passed, double time_to_first_token,
                            double token_generation_rate,
                            const QString &error_message,
                            const gui::SystemInfoDetails &sys_info,
                            QWidget *parent = nullptr);
  void SetSelected(bool selected);
  bool IsPassed();
 signals:
  void OpenButtonClicked();
  void SelectionBoxChecked(bool checked);

 private:
  Ui::ResultsHistoryEntryWidget ui_;
  bool is_passed_;
};

#endif  // RESULTS_HISTORY_ENTRY_WIDGET_H_