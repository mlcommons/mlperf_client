#ifndef LOADING_WIDGET_H_
#define LOADING_WIDGET_H_

#include "popup_widget.h"

class QLabel;
class QTimer;
class CircularProgressBar;

class LoadingWidget : public PopupWidget {
  Q_OBJECT
 public:
  explicit LoadingWidget(QWidget* parent = nullptr);

  void SetProgressPercent(int percent);
  void SetEtaMs(qint64 remaining_ms);
  void ShowEtaPlaceholder();
  void ClearEta();

  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void RefreshEtaLabel();
  void TickCountdown();

  CircularProgressBar* spinner_;
  QLabel* progress_percent_label_;
  QLabel* eta_label_;
  QTimer* countdown_timer_;

  bool eta_active_ = false;
  double displayed_eta_secs_ = 0.0;
};

#endif  // LOADING_WIDGET_H_
