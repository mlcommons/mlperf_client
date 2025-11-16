#ifndef TOGGLE_BUTTON_H
#define TOGGLE_BUTTON_H

#include <QAbstractButton>

class QPropertyAnimation;

/**
 * @class ToggleButton
 * @brief Animated toggle switch with sliding indicator.
 */
class ToggleButton : public QAbstractButton {
  Q_OBJECT
  Q_PROPERTY(int CirclePosition READ GetCirclePosition WRITE SetCirclePosition)

 public:
  ToggleButton(bool checked = false, QWidget* parent = nullptr);
  void SetFixedSize(int width, int height, int circle_diameter);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private slots:
  void OnChecked(bool checked);

 private:
  int GetCirclePosition() const;
  int GetCircleStartPosition() const;
  int GetCircleEndPosition() const;
  void SetCirclePosition(int position);

  int circle_position_;
  int circle_diameter_;
  QPropertyAnimation* animation_;
};

#endif  // TOGGLE_BUTTON_H