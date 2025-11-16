#ifndef TAB_BAR_BUTTON_H
#define TAB_BAR_BUTTON_H

#include <QAbstractButton>

/**
 * @class TabBarButton
 * @brief Custom button for tab bar interfaces.
 */
class TabBarButton : public QAbstractButton {
  Q_OBJECT

 public:
  TabBarButton(QWidget* parent = nullptr);

  /**
   * @brief Returns the minimum size hint for the button.
   */
  QSize minimumSizeHint() const;

 protected:
  void paintEvent(QPaintEvent* event) override;
};

#endif  // TAB_BAR_BUTTON_H