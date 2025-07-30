#ifndef POPUP_WIDGET_H_
#define POPUP_WIDGET_H_

#include <QDialog>

class QLabel;
class QPushButton;

class PopupWidget : public QDialog {
  Q_OBJECT

 public:
  explicit PopupWidget(QWidget* parent = nullptr, bool is_question_popup = false);
  void SetMessage(const QString& message);
  static void ShowMessage(const QString& message, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QLabel* message_label_;
  QPushButton* close_button_;
};

#endif  // POPUP_WIDGET_H_
