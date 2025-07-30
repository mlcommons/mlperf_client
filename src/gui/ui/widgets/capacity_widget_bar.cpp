#include "capacity_widget_bar.h"

#include <QProgressBar>
#include <QString>

#include "result_component_wrapper_widget.h"

CapacityWidgetBar::CapacityWidgetBar(const QString &title,
                                     const QString tooltip_text,
                                     const QString unit, QWidget *parent)
    : QProgressBar(parent), unit_(unit), max_value_(0.0), value_(0.0) {
  setObjectName("capacityWidgetBar");
  setTextVisible(false);
  setMinimumHeight(35);
  wrapper_widget_ =
      new ResultComponentWrapperWidget(this, title, QIcon(), "", tooltip_text);
  setMaximum(100);
  SetValue(0);
}

void CapacityWidgetBar::SetValue(double value) {
  value_ = value;
  int new_value = static_cast<int>(value / max_value_ * 100);
  setValue(new_value);
  update();
}

void CapacityWidgetBar::SetMaximum(double value) {
  max_value_ = value;
  SetValue(value_);
  update();
}

void CapacityWidgetBar::SetUnit(const QString &unit) {
  unit_ = unit;
  update();
}

void CapacityWidgetBar::update() {
  QString text = QString::number(value_, 'f', 1) + "/" +
                 QString::number(max_value_, 'f', 1) + " " + unit_;
  wrapper_widget_->SetBottomText(text);
  QProgressBar::update();
}