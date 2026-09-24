#ifndef CIRCULAR_PROGRESS_BAR_WIDGET_H
#define CIRCULAR_PROGRESS_BAR_WIDGET_H

#include <QElapsedTimer>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QWidget>

/**
 * @class CircularProgressBar
 * @brief Animated circular progress bar with customizable styling.
 */
class CircularProgressBar : public QWidget {
  Q_OBJECT
  Q_PROPERTY(double GradientRotationAngle READ GradientRotationAngle WRITE
                 SetGradientRotationAngle)

 public:
  CircularProgressBar(int diameter, QWidget *parent = nullptr);

  void SetProgress(int value) {
    if (m_value == value) return;
    m_value = value;
    m_cache_dirty = true;
    update();
  }

  /**
   * @brief Progress bar configs.
   */
  void SetThickness(int thickness) {
    if (m_thickness == thickness) return;
    m_thickness = thickness;
    m_cache_dirty = true;
    update();
  }
  void SetBackgroundColor(QColor background_color) {
    if (m_background_color == background_color) return;
    m_background_color = background_color;
    m_cache_dirty = true;
    update();
  }
  void SetInnerBackgroundColor(QColor inner_background_color) {
    if (m_inner_background_color == inner_background_color) return;
    m_inner_background_color = inner_background_color;
    m_cache_dirty = true;
    update();
  }
  void SetArcColor(QColor arc_color) {
    if (m_arc_color == arc_color) return;
    m_arc_color = arc_color;
    m_cache_dirty = true;
    update();
  }
  void SetTextColor(QColor textColor) {
    if (m_textColor == textColor) return;
    m_textColor = textColor;
    m_cache_dirty = true;
    update();
  }
  void SetBottomText(QString bottom_text) {
    if (m_bottom_text == bottom_text) return;
    m_bottom_text = bottom_text;
    m_cache_dirty = true;
    update();
  }
  void SetSymbol(QString symbol) {
    if (m_symbol == symbol) return;
    m_symbol = symbol;
    m_cache_dirty = true;
    update();
  }
  void SetCenterTextVisible(bool center_text_visible) {
    if (m_center_text_visible == center_text_visible) return;
    m_center_text_visible = center_text_visible;
    m_cache_dirty = true;
    update();
  }
  void SetBottomTextVisible(bool bottom_text_visible) {
    if (m_bottom_text_visible == bottom_text_visible) return;
    m_bottom_text_visible = bottom_text_visible;
    m_cache_dirty = true;
    update();
  }
  void SetDiameter(int diameter) {
    if (m_diameter == diameter) return;
    m_cache_dirty = true;
    this->setFixedSize(diameter, diameter);
    m_diameter = diameter;
    update();
  }
  void SetAddInnerCircle(bool add_inner_circle) {
    if (m_add_inner_circle == add_inner_circle) return;
    m_add_inner_circle = add_inner_circle;
    m_cache_dirty = true;
    update();
  }
  void SetIconPath(QString icon_path) {
    if (m_icon_path == icon_path) return;
    m_icon_path = icon_path;
    m_cache_dirty = true;
    update();
  }

  /**
   * @brief Gets the current progress value.
   */
  int GetProgress() { return m_value; }

  /**
   * @brief Get/Set gradient rotation angle
   */
  double GradientRotationAngle() const { return m_gradient_rotation_angle; }

  void SetGradientRotationAngle(double angle) {
    m_gradient_rotation_angle = angle;
    update();
  }

 protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void changeEvent(QEvent *event) override;

 private:
  int m_diameter;
  int m_value;
  int m_thickness;
  QString m_bottom_text;
  QString m_symbol;
  QString m_icon_path;
  bool m_center_text_visible;
  bool m_bottom_text_visible;
  bool m_add_inner_circle;
  QColor m_inner_background_color;
  QColor m_background_color;
  QColor m_arc_color;
  QColor m_textColor;

  double m_gradient_rotation_angle;
  QTimer *m_animation_timer = nullptr;
  QElapsedTimer m_rotation_clock;
  QPixmap m_ring_cache;
  QPixmap m_content_cache;
  bool m_cache_dirty = true;
  void rebuildCaches();
  void setup_ui();
  void connect_signals();
};

#endif  // CIRCULAR_PROGRESS_BAR_WIDGET_H
