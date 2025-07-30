#ifndef CIRCULAR_PROGRESS_BAR_WIDGET_H
#define CIRCULAR_PROGRESS_BAR_WIDGET_H

#include <QLabel>
#include <QPainter>
#include <QWidget>

class QPropertyAnimation;

class CircularProgressBar : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int GradientRotationAngle READ GradientRotationAngle WRITE
                 SetGradientRotationAngle)

 public:
  // Constructor
  CircularProgressBar(int diameter, QWidget *parent = nullptr);
  void SetProgress(int value) {
    m_value = value;
    update();
  }
  void SetThickness(int thickness) {
    m_thickness = thickness;
    update();
  }
  void SetBackgroundColor(QColor background_color) {
    m_background_color = background_color;
    update();
  }
  void SetInnerBackgroundColor(QColor inner_background_color) {
    m_inner_background_color = inner_background_color;
    update();
  }
  void SetArcColor(QColor arc_color) {
    m_arc_color = arc_color;
    update();
  }
  void SetTextColor(QColor textColor) {
    m_textColor = textColor;
    update();
  }
  void SetBottomText(QString bottom_text) {
    m_bottom_text = bottom_text;
    update();
  }
  void SetSymbol(QString symbol) {
    m_symbol = symbol;
    update();
  }
  void SetCenterTextVisible(bool center_text_visible) {
    m_center_text_visible = center_text_visible;
    update();
  }
  void SetBottomTextVisible(bool bottom_text_visible) {
    m_bottom_text_visible = bottom_text_visible;
    update();
  }
  void SetDiameter(int diameter) {
    this->setFixedSize(diameter, diameter);
    m_diameter = diameter;
    update();
  }
  void SetAddInnerCircle(bool add_inner_circle) {
    m_add_inner_circle = add_inner_circle;
    update();
  }
  void SetIconPath(QString icon_path) {
    m_icon_path = icon_path;
    update();
  }
  int GetProgress() { return m_value; }

  int GradientRotationAngle() const {
    return m_gradient_rotation_angle;
  }

  void SetGradientRotationAngle(int angle) {
    m_gradient_rotation_angle = angle;
    update();
  }

 protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

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

  int m_gradient_rotation_angle;
  QPropertyAnimation *m_gradient_animation;

  void setup_ui();
  void connect_signals();
};

#endif  // CIRCULAR_PROGRESS_BAR_WIDGET_H
