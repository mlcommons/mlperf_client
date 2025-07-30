#include "tab_bar_button.h"

#include <QPaintEvent>
#include <QPainter>

namespace {
static constexpr int BUTTON_UNDERLINE_HEIGHT = 1;
}  // namespace

TabBarButton::TabBarButton(QWidget *parent) : QAbstractButton(parent) {
  setCheckable(true);
  setChecked(false);
}

QSize TabBarButton::minimumSizeHint() const {
  QString s(text());
  bool empty = s.isEmpty();
  if (empty) s = QString::fromLatin1("XXXX");
  QFontMetrics fm = fontMetrics();
  QSize fm_size = fm.size(Qt::TextSingleLine, s);
  // allocate some space for the underline
  return QSize(fm_size.width() * 1.3,
               fm_size.height() * 1.5 + BUTTON_UNDERLINE_HEIGHT);
}

void TabBarButton::paintEvent(QPaintEvent *event) {
  QPainter painter(this);

  painter.setFont(font());
  painter.setPen(isEnabled() ? QColor(Qt::white) : QColor(200, 200, 200));
  painter.setBrush(QColor(Qt::transparent));

  painter.drawText(QRect(0, 0, width(), height() - BUTTON_UNDERLINE_HEIGHT),
                   Qt::AlignCenter, text());
  if (isChecked()) {
    painter.setBrush(QColor(Qt::white));
    painter.drawRect(QRect(0, height() - BUTTON_UNDERLINE_HEIGHT, width(),
                           BUTTON_UNDERLINE_HEIGHT));
  }
  event->accept();
}