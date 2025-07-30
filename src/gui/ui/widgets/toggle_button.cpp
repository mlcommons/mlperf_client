#include "toggle_button.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPropertyAnimation>

namespace {
static constexpr int BUTTON_WIDTH = 35;
static constexpr int BUTTON_HEIGHT = 20;
static constexpr int CIRCLE_START_POSITION = 3;
static constexpr int CIRCLE_DIAMETER = 14;
}  // namespace

ToggleButton::ToggleButton(bool checked, QWidget *parent)
    : QAbstractButton(parent), circle_position_(CIRCLE_START_POSITION) {
  setCheckable(true);
  setChecked(checked);
  setFixedSize(BUTTON_WIDTH, BUTTON_HEIGHT);

  circle_position_ = checked
                         ? width() - (CIRCLE_DIAMETER + CIRCLE_START_POSITION)
                         : CIRCLE_START_POSITION;

  animation_ = new QPropertyAnimation(this, "CirclePosition", this);
  animation_->setDuration(150);

  connect(this, &QAbstractButton::toggled, this, &ToggleButton::OnChecked);
}

void ToggleButton::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.setPen(Qt::NoPen);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::SolidPattern);
  painter.setBrush(QColor::fromString("#7A919EAB"));
  painter.drawRoundedRect(QRect(0, 0, width(), height()), height() / 2.0,
                          height() / 2.0);
  painter.setBrush(isChecked() ? QColor::fromString("#EEA02B")
                               : QColor::fromString("#ffffff"));
  painter.drawEllipse(QRectF(GetCirclePosition(),
                             (height() - CIRCLE_DIAMETER) / 2, CIRCLE_DIAMETER,
                             CIRCLE_DIAMETER));
  event->accept();
}

void ToggleButton::OnChecked(bool checked) {
  int startValue = CIRCLE_START_POSITION;
  int endValue = width() - (CIRCLE_DIAMETER + CIRCLE_START_POSITION);

  if (!checked) qSwap(startValue, endValue);

  animation_->setStartValue(startValue);
  animation_->setEndValue(endValue);
  animation_->start();
}

int ToggleButton::GetCirclePosition() const { return circle_position_; }

void ToggleButton::SetCirclePosition(int position) {
  circle_position_ = position;
  update();
}