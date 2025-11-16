#include "toggle_button.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPropertyAnimation>

namespace {
static constexpr int BUTTON_DEFAULT_WIDTH = 35;
static constexpr int BUTTON_DEFAULT_HEIGHT = 20;
static constexpr int CIRCLE_DEFAULT_DIAMETER = 14;
}  // namespace

ToggleButton::ToggleButton(bool checked, QWidget *parent)
    : QAbstractButton(parent) {
  setCheckable(true);
  setChecked(checked);

  SetFixedSize(BUTTON_DEFAULT_WIDTH, BUTTON_DEFAULT_HEIGHT,
               CIRCLE_DEFAULT_DIAMETER);

  animation_ = new QPropertyAnimation(this, "CirclePosition", this);
  animation_->setDuration(150);

  connect(this, &QAbstractButton::toggled, this, &ToggleButton::OnChecked);
}

void ToggleButton::SetFixedSize(int width, int height, int circle_diameter) {
  setFixedSize(width, height);

  circle_diameter_ = circle_diameter;
  int start_position = GetCircleStartPosition();
  circle_position_ =
      isChecked() ? GetCircleEndPosition() : GetCircleStartPosition();
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
  painter.drawEllipse(QRectF(GetCirclePosition(), GetCircleStartPosition(),
                             circle_diameter_, circle_diameter_));
  event->accept();
}

void ToggleButton::OnChecked(bool checked) {
  int startValue = GetCircleStartPosition();
  int endValue = GetCircleEndPosition();

  if (!checked) qSwap(startValue, endValue);

  animation_->setStartValue(startValue);
  animation_->setEndValue(endValue);
  animation_->start();
}

int ToggleButton::GetCirclePosition() const { return circle_position_; }

int ToggleButton::GetCircleStartPosition() const {
  return (height() - circle_diameter_) / 2;
}

int ToggleButton::GetCircleEndPosition() const {
  return width() - circle_diameter_ - GetCircleStartPosition();
}

void ToggleButton::SetCirclePosition(int position) {
  circle_position_ = position;
  update();
}