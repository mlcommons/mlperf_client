#include "thermometer_widget.h"

#include <QBrush>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include "result_component_wrapper_widget.h"

ThermometerWidget::ThermometerWidget(const QString &title, const QIcon &icon,
                                     const QString tooltip_text,
                                     QWidget *parent)
    : QWidget(parent),
      min_temperature_(0.0),
      max_temperature_(100.0),
      current_temperature_(0.0) {
  setMinimumSize(158, 60);
  wrapper_widget_ =
      new ResultComponentWrapperWidget(this, title, icon, "", tooltip_text);
}

void ThermometerWidget::SetRange(double min_temp, double max_temp) {
  min_temperature_ = min_temp;
  max_temperature_ = max_temp;
  update();
}

void ThermometerWidget::SetTemperature(double temperature) {
  current_temperature_ = temperature;
  update();
}

void ThermometerWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Define key dimensions
  int bulb_diameter = 48;
  int inner_bulb_diameter = 31;
  int tube_height = 24;
  int left_padding = 6;
  int tube_length = width() - bulb_diameter - 20 - left_padding;
  int border_thickness = 6;

  // Calculate positions
  int bulb_x = left_padding;
  int bulb_y = height() / 2 - bulb_diameter / 2;
  int tube_x = left_padding + bulb_diameter - tube_height / 2;
  int tube_y = height() / 2 - tube_height / 2;

  // Calculate the temperature fill
  double temperature_ratio = (current_temperature_ - min_temperature_) /
                             (max_temperature_ - min_temperature_);
  temperature_ratio = qBound(0.0, temperature_ratio, 1.0);
  int fill_length = static_cast<int>(tube_length * temperature_ratio);
  int fill_x = tube_x + fill_length;

  // Create the unified thermometer path
  QPainterPath thermometer_path;
  thermometer_path.addEllipse(bulb_x, bulb_y, bulb_diameter, bulb_diameter);
  thermometer_path.moveTo(tube_x, tube_y);
  thermometer_path.lineTo(tube_x + tube_length, tube_y);
  thermometer_path.arcTo(tube_x + tube_length, tube_y, tube_height, tube_height,
                         90, -180);
  thermometer_path.lineTo(tube_x, tube_y + tube_height);
  thermometer_path.closeSubpath();

  // Draw the thermometer outline
  painter.setPen(QPen(QColor("#154060"), border_thickness));
  painter.setBrush(QColor("#154060"));
  painter.drawPath(thermometer_path);

  // Draw a solid circle for the bulb of the thermometer
  painter.setBrush(QBrush(QColor("#154060")));
  painter.drawEllipse(bulb_x, bulb_y, bulb_diameter, bulb_diameter);

  // Draw the inner bulb circle
  painter.setBrush(QBrush(QColor("#EEA02B")));
  painter.drawEllipse(bulb_x + (bulb_diameter - inner_bulb_diameter) / 2,
                      bulb_y + (bulb_diameter - inner_bulb_diameter) / 2,
                      inner_bulb_diameter, inner_bulb_diameter);

  if (temperature_ratio > 0.0) {
    // Adjust the tube_x for the fill
    int adjusted_tube_x =
        bulb_x + (bulb_diameter + inner_bulb_diameter) / 2 - tube_height / 2;

    // Draw the temperature tube fill
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#EEA02B"));

    QPainterPath temperature_fill_path;
    temperature_fill_path.moveTo(adjusted_tube_x, tube_y + border_thickness);

    if (fill_x < tube_x + tube_length) {
      // Normal case: fill doesn't reach the end
      temperature_fill_path.lineTo(fill_x, tube_y + border_thickness);
      temperature_fill_path.lineTo(fill_x,
                                   tube_y + tube_height - border_thickness);
    } else {
      // Fill reaches or exceeds the end of the tube
      temperature_fill_path.lineTo(tube_x + tube_length,
                                   tube_y + border_thickness);
      temperature_fill_path.arcTo(tube_x + tube_length,
                                  tube_y + border_thickness,
                                  tube_height - 2 * border_thickness,
                                  tube_height - 2 * border_thickness, 90, -180);
    }

    temperature_fill_path.lineTo(adjusted_tube_x,
                                 tube_y + tube_height - border_thickness);
    temperature_fill_path.closeSubpath();

    painter.drawPath(temperature_fill_path);

    // Add circular progress effect
    int progress_diameter = tube_height - 2 * border_thickness;
    int progress_y = tube_y + border_thickness;
    int progress_x;
    progress_x = qMin(fill_x - progress_diameter / 2,
                      tube_x + tube_length - progress_diameter);
    painter.setBrush(QColor("#EEA02B"));
    painter.drawEllipse(progress_x, progress_y, progress_diameter,
                        progress_diameter);
  }
  QString text =
      QString::number(static_cast<int>(current_temperature_)) + "°C ( " +
      QString::number(static_cast<int>(current_temperature_ * 1.8 + 32)) +
      "°F )";
  wrapper_widget_->SetBottomText(text);
}
