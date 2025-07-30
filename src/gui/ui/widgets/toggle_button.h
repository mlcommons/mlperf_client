#ifndef TOGGLE_BUTTON_H
#define TOGGLE_BUTTON_H

#include <QAbstractButton>

class QPropertyAnimation;

class ToggleButton : public QAbstractButton {
  Q_OBJECT
  Q_PROPERTY(int CirclePosition READ GetCirclePosition WRITE SetCirclePosition)
 public:
  ToggleButton(bool checked = false, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private slots:
  void OnChecked(bool checked);

 private:
  int GetCirclePosition() const;
  void SetCirclePosition(int position);

  int circle_position_;
  QPropertyAnimation* animation_;
};

#endif  // TOGGLE_BUTTON_H
