#include "circular_gauge_widget.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QtMath>

#include "result_component_wrapper_widget.h"

CircularGauge::CircularGauge(const QString &title, const QIcon &icon,
                             const QString tooltip_text, QWidget *parent)
    : QWidget(parent), m_value(0) {
  setup_ui();
  wrapper_widget_ =
      new ResultComponentWrapperWidget(this, title, icon, "", tooltip_text);
  SetValue(0);
}

void CircularGauge::setup_ui() {
  setMinimumSize(168, 84);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  QString svgContent = R"(
<svg width="168" height="84" viewBox="0 0 168 84" fill="none" xmlns="http://www.w3.org/2000/svg">
<path d="M167.332 83.666C167.332 61.4764 158.517 40.1956 142.827 24.5052C127.136 8.81479 105.856 1.69501e-06 83.666 1.97449e-08C61.4764 -1.65552e-06 40.1956 8.81478 24.5052 24.5052C8.81479 40.1956 3.46405e-06 61.4764 1.13517e-07 83.666H20.9165C20.9165 67.0238 27.5276 51.0632 39.2954 39.2954C51.0632 27.5276 67.0238 20.9165 83.666 20.9165C100.308 20.9165 116.269 27.5276 128.037 39.2954C139.804 51.0632 146.416 67.0238 146.416 83.666H167.332Z" fill="#154060"/>
<path d="M43.145 10.4673C30.0352 17.7246 19.115 28.3695 11.5255 41.2898C3.93594 54.2101 -0.0443932 68.9315 0.000373488 83.916L20.9168 83.8535C20.8832 72.6152 23.8685 61.5741 29.5606 51.8838C35.2528 42.1936 43.4429 34.2099 53.2753 28.767L43.145 10.4673Z" fill="#6ACAFF"/>
<path d="M125.499 11.2091C112.812 3.8842 98.4238 0.0189728 83.774 6.967e-05C69.1241 -0.0188335 54.7261 3.80925 42.0201 11.1014L52.4316 29.2426C61.9611 23.7734 72.7596 20.9024 83.747 20.9166C94.7344 20.9307 105.525 23.8297 115.041 29.3233L125.499 11.2091Z" fill="#3D86BD"/>
</svg>
    )";
  m_svg_renderer.load(svgContent.toUtf8());
}

void CircularGauge::SetValue(double value) {
  m_value = qBound(0.0, value, 100.0);
  update();
}

void CircularGauge::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Calculate the scaling factor to maintain aspect ratio
  qreal text_space = 0;
  qreal width_ratio = width() / 168.0;
  qreal height_ratio = (height() - text_space) / 84.0;
  qreal scale_factor = qMin(width_ratio, height_ratio);

  // Calculate the position to center the gauge
  qreal x_gauge_offset = (width() - 168 * scale_factor) / 2;
  qreal y_gauge_offset = (height() - text_space - 84 * scale_factor) / 2;

  painter.save();
  painter.translate(x_gauge_offset, y_gauge_offset);
  painter.scale(scale_factor, scale_factor);
  m_svg_renderer.render(&painter, QRectF(0, 0, 168, 84));
  painter.restore();

  // Handle needle dimensions and position
  qreal center_x = x_gauge_offset + 84 * scale_factor;
  qreal center_y = y_gauge_offset + 84 * scale_factor - 5 * scale_factor;
  qreal needle_length = 84 * scale_factor * 0.9;

  painter.save();
  painter.translate(center_x, center_y);
  // -90 to start from the left, 1.8 to cover 180 degrees
  painter.rotate(-90 + m_value * 1.8);

  QPainterPath needle_path;
  needle_path.moveTo(-2 * scale_factor, 0);
  needle_path.lineTo(0, -needle_length);
  needle_path.lineTo(2 * scale_factor, 0);
  needle_path.closeSubpath();

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor("#EEA02B"));
  painter.drawPath(needle_path);

  painter.drawEllipse(QPointF(0, 0), 5 * scale_factor, 5 * scale_factor);
  painter.restore();
  //   // Draw the text
  //   painter.save();
  //   painter.setPen(Qt::white);
  //   painter.setFont(QFont("Roboto", 24, 400));
  //   QString text = QString::number(static_cast<int>(m_value));
  //   text += "%";
  //   QRect text_rect = QRect(0, center_y + 10, width(), text_space);
  //   painter.drawText(text_rect, Qt::AlignCenter, text);
  //   painter.restore();
  wrapper_widget_->SetBottomText(QString::number(static_cast<int>(m_value)) +
                                 "%");
}
