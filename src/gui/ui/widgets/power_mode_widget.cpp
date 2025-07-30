#include "power_mode_widget.h"

#include <QLabel>

#include "result_component_wrapper_widget.h"

PowerModeWidget::PowerModeWidget(const QString &title, const QIcon &icon,
                                 const QString tooltip_text, QWidget *parent)
    : QLabel{parent} {
  setObjectName("powerModeWidget");
  setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  wrapper_widget_ =
      new ResultComponentWrapperWidget(this, title, icon, "", tooltip_text);
  wrapper_widget_->hideBottomText();
  SetMode("Unknown");
}

void PowerModeWidget::SetMode(const QString &mode) { setText(mode); }
