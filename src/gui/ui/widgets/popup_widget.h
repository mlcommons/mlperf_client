#ifndef POPUP_WIDGET_H_
#define POPUP_WIDGET_H_

#include <QDialog>

class QLabel;
class QPushButton;

/**
 * @class PopupWidget
 * @brief Modal dialog widget for message display.
 */
class PopupWidget : public QDialog {
  Q_OBJECT

 public:
  /**
   * @brief Construct popup widget.
   * @param parent Parent widget, defaults to nullptr.
   * @param is_question_popup Boolean indicating if this is a question popup, defaults to false.
   */
  explicit PopupWidget(QWidget* parent = nullptr, bool is_question_popup = false);
  
  /**
   * @brief Set the message text to display in the popup.
   */
  void SetMessage(const QString& message);

  /**
   * @brief Static convenience method to show a message popup.
   */
  static void ShowMessage(const QString& message, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QLabel* message_label_;
  QPushButton* close_button_;
};

#endif  // POPUP_WIDGET_H_