#ifndef RESULT_WRAPPER_WIDGET_H
#define RESULT_WRAPPER_WIDGET_H

#include <QIcon>
#include <QLabel>
#include <QWidget>

class QLabel;
class QHBoxLayout;

class ResultComponentWrapperWidget : public QWidget {
 public:
  ResultComponentWrapperWidget(QWidget* center_widget, const QString& top_text,
                               const QIcon& icon, const QString& bottom_text,
                               const QString tooltip_text,
                               QWidget* parent = nullptr);
  void SetBottomText(const QString& bottom_text) {
    bottom_text_label_->setText(bottom_text);
  }
  void hideBottomText() { bottom_text_label_->hide(); }

 private:
  void setup_ui(const QString& top_text, const QIcon& icon,
                const QString& bottom_text, const QString tooltip_text);

  QWidget* center_widget_;
  QLabel* top_info_label_;
  QLabel* icon_label_;
  QLabel* text_label_;
  QHBoxLayout* center_layout_;
  QLabel* bottom_text_label_;
};

#endif  // RESULT_WRAPPER_WIDGET_H
