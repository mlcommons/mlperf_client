#ifndef EXECUTION_PROGRESS_WIDGET_H
#define EXECUTION_PROGRESS_WIDGET_H

#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "circular_progress_bar_widget.h"
#include "ep_progress_widget.h"
#include "toggle_button.h"

class ExecutionProgressWidget : public QWidget {
  Q_OBJECT
 public:
  explicit ExecutionProgressWidget(QWidget *parent = nullptr);
  void SetProgress(int value);
  void SetEpSelected(int index);
  int GetCurrentSelectedEP() const;
  int GetTotalEPs() const;
  void SetSelectedEpProgress(int total_steps, int current_step);
  int AddEPProgressWidget(QString name, QString description, QString icon_path,
                          QString long_name);
  void MoveToNextEP(bool current_success);
  void ClearEPs();
  void AddEP(const QString &name, const QString &description,
             const QString &device_type, const QString &long_name);
  void SetTaskName(const QString &name);
  void SetCancelingState(bool on);

 private:
  ToggleButton *m_hide_counters_button;
  QLabel *m_hide_counters_label;
  CircularProgressBar *m_progress_bar;
  QVBoxLayout *m_ep_layout;
  QList<EPProgressWidget *> m_ep_widgets;
  QLabel *m_alert_label;
  QLabel *m_alert_icon;
  QPushButton *m_cancel_button;
  int current_selected_ep = -1;
  void setup_ui();
  void setup_connections();
 signals:
  void HideCountersToggled(bool checked);
  void CancelClicked();
};

#endif  // EXECUTION_PROGRESS_WIDGET_H
