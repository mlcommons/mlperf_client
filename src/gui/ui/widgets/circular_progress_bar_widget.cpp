#include "circular_progress_bar_widget.h"

#include <QPropertyAnimation>

CircularProgressBar::CircularProgressBar(int diameter, QWidget *parent)
    : QWidget(parent),
      m_diameter(diameter),
      m_value(0),
      m_thickness(6),
      m_bottom_text(""),
      m_symbol("%"),
      m_center_text_visible(false),
      m_bottom_text_visible(false),
      m_add_inner_circle(false),
      m_arc_color(QColor(255, 255, 215, 200)),
      m_textColor(QColor(255, 255, 255)),
      m_background_color(Qt::transparent),
      m_inner_background_color(QColor("#135384")),
      m_gradient_rotation_angle(0) {
  setup_ui();

  m_gradient_animation =
      new QPropertyAnimation(this, "GradientRotationAngle", this);
  m_gradient_animation->setStartValue(0);
  m_gradient_animation->setEndValue(360);
  m_gradient_animation->setDuration(4000);
  m_gradient_animation->setLoopCount(-1);
  m_gradient_animation->start();
}

void CircularProgressBar::setup_ui() {
  this->setFixedSize(m_diameter, m_diameter);
  this->setWindowFlags(Qt::FramelessWindowHint);
  this->setAttribute(Qt::WA_TranslucentBackground);
  this->setWindowFlags(Qt::WindowStaysOnTopHint);
  this->update();
}

void CircularProgressBar::connect_signals() {}

void CircularProgressBar::paintEvent(QPaintEvent *event) {
  auto width = this->width() - m_thickness;
  auto height = this->height() - m_thickness;
  auto margin = m_thickness / 2;

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  // Draw the outer Rectangle of the widget without the border
  auto rect = QRect(0, 0, this->width(), this->height());
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::transparent);
  painter.drawRect(rect);
  // Draw the arc of the progress bar
  auto pen = QPen(Qt::white, m_thickness);
  pen.setCapStyle(Qt::RoundCap);
  QConicalGradient gradient(margin + width / 2, margin + height / 2,
                            90 - m_gradient_rotation_angle);
  auto start_color = m_arc_color;
  start_color.setAlpha(30);
  gradient.setColorAt(0.0, start_color);
  gradient.setColorAt(0.3, start_color);
  gradient.setColorAt(0.5, m_arc_color);
  gradient.setColorAt(0.7, start_color);
  gradient.setColorAt(1.0, start_color);
  pen.setBrush(gradient);
  painter.setPen(pen);
  painter.drawArc(margin, margin, width, height, 0, 360 * 16);
  auto inner_radius = width / 2 - m_thickness - 8;
  // Draw the inner circle
  if (m_add_inner_circle) {
    painter.setPen(Qt::NoPen);
    // Define the QLinearGradient
    QLinearGradient linear_gradient(
        QPointF(width / 2 + margin, margin),
        QPointF(width / 2 + margin, height / 2 + margin));
    linear_gradient.setColorAt(0.0, m_inner_background_color);
    linear_gradient.setColorAt(1.0, m_inner_background_color);
    painter.setBrush(linear_gradient);
    painter.drawEllipse(QPoint(width / 2 + margin, height / 2 + margin),
                        inner_radius, inner_radius);
  }
  if (m_center_text_visible) {
    auto font = painter.font();
    font.setPixelSize(47);
    font.setFamily("Roboto");
    font.setWeight(QFont::Weight::Medium);
    painter.setFont(font);
    painter.setPen(m_textColor);
    auto text_to_draw = QString::number(m_value) + m_symbol;
    auto text_rect = painter.fontMetrics().boundingRect(text_to_draw);
    int x_start = this->width() / 2 - text_rect.width() / 2;
    int y_start = this->height() / 2 + text_rect.height() / 4;
    painter.drawText(x_start, y_start, text_to_draw);
  }
  if (m_bottom_text_visible) {
    auto font = painter.font();
    font.setPixelSize(14);
    font.setWeight(QFont::Weight::Medium);
    painter.setFont(font);
    painter.setPen(m_textColor);
    auto text_to_draw = m_bottom_text;
    auto text_rect = painter.fontMetrics().boundingRect(text_to_draw);
    painter.drawText(width / 2 - text_rect.width() / 2 + margin,
                     height / 2 + inner_radius / 2 + margin, text_to_draw);
  }
  // Draw the icon above the center text
  if (!m_icon_path.isEmpty()) {
    auto icon = QPixmap(m_icon_path);
    int icon_center_y = this->height() / 2 - inner_radius / 2;
    int icon_center_x = this->width() / 2;
    painter.drawPixmap(icon_center_x - icon.width() / 2,
                       icon_center_y - icon.height() * 0.66, icon);
  }
  // done
}

void CircularProgressBar::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  m_gradient_animation->start();
}

void CircularProgressBar::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  m_gradient_animation->stop();
}