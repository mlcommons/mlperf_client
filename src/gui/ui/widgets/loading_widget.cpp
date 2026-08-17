#include "loading_widget.h"

#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QTimer>
#include <cmath>

#include "circular_progress_bar_widget.h"

static constexpr int LOADING_SPINNER_DIAMETER = 56;

namespace {

QString FormatEta(qint64 ms) {
  qint64 total_sec = (ms + 999) / 1000;
  if (total_sec < 1) total_sec = 1;
  if (total_sec < 60) return QString("%1 sec").arg(total_sec);

  const qint64 minutes = total_sec / 60;
  const qint64 seconds = total_sec % 60;
  if (seconds == 0) return QString("%1 min").arg(minutes);
  return QString("%1 min %2 sec").arg(minutes).arg(seconds);
}

}  // namespace

LoadingWidget::LoadingWidget(QWidget* parent) : PopupWidget(NoIcon{}) {
  // Reparent dialog to clear window-system properties per Qt documentation.
  setParent(parent);

  spinner_ = new CircularProgressBar(LOADING_SPINNER_DIAMETER, this);
  spinner_->SetThickness(5);

  // The percentage is rendered centered inside the rotating circle.
  progress_percent_label_ = new QLabel("0%", spinner_);
  progress_percent_label_->setProperty("class", "medium_strong_label");
  progress_percent_label_->setAlignment(Qt::AlignCenter);
  progress_percent_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  progress_percent_label_->setGeometry(0, 0, LOADING_SPINNER_DIAMETER,
                                       LOADING_SPINNER_DIAMETER);

  main_layout_->insertWidget(1, spinner_, 0, Qt::AlignCenter);

  eta_label_ = new QLabel(this);
  eta_label_->setProperty("class", "medium_regular_label");
  eta_label_->setAlignment(Qt::AlignCenter);
  eta_label_->setText("Please wait");

  main_layout_->addWidget(eta_label_, 0, Qt::AlignCenter);
  main_layout_->addStretch();

  // 1 Hz countdown so the ETA visibly decreases between fresh estimates.
  countdown_timer_ = new QTimer(this);
  countdown_timer_->setInterval(1000);
  connect(countdown_timer_, &QTimer::timeout, this,
          &LoadingWidget::TickCountdown);

  if (parent) {
    move((parentWidget()->width() - width()) / 2,
         (parentWidget()->height() - height()) / 2);
    parent->installEventFilter(this);
  }
}

void LoadingWidget::SetProgressPercent(int percent) {
  progress_percent_label_->setText(QString("%1%").arg(percent));
}

void LoadingWidget::SetEtaMs(qint64 remaining_ms) {
  displayed_eta_secs_ = remaining_ms / 1000.0;
  eta_active_ = true;
  if (!countdown_timer_->isActive()) countdown_timer_->start();
  RefreshEtaLabel();
}

void LoadingWidget::ShowEtaPlaceholder() {
  eta_active_ = false;
  countdown_timer_->stop();
  eta_label_->setText("Please wait");
}

void LoadingWidget::ClearEta() {
  eta_active_ = false;
  countdown_timer_->stop();
  eta_label_->clear();
}

void LoadingWidget::RefreshEtaLabel() {
  // Hold at "1 sec" instead of dropping to 0 so the countdown doesn't visibly
  // bottom out while the last bytes finish or the next stage is preparing.
  qint64 eta_ms = static_cast<qint64>(std::round(displayed_eta_secs_ * 1000.0));
  if (eta_ms < 1000) eta_ms = 1000;
  eta_label_->setText(QString("About %1 remaining").arg(FormatEta(eta_ms)));
}

void LoadingWidget::TickCountdown() {
  if (!eta_active_) return;
  if (displayed_eta_secs_ > 1.0) displayed_eta_secs_ -= 1.0;
  RefreshEtaLabel();
}

bool LoadingWidget::eventFilter(QObject* watched, QEvent* event) {
  auto parent = parentWidget();
  if (watched == parent &&
      (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
    move((parent->width() - width()) / 2, (parent->height() - height()) / 2);
  }
  return PopupWidget::eventFilter(watched, event);
}
