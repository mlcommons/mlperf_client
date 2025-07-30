#ifndef EXPANDABLE_WIDGET_H_
#define EXPANDABLE_WIDGET_H_

#include <QWidget>
;
class QPushButton;
class QPropertyAnimation;

class ExpandableWidget : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int contentHeight WRITE SetContentHeight)
 public:
  explicit ExpandableWidget(QWidget *parent = nullptr);
  void Expand();

 private slots:
  void ToggleExpand(bool is_expanded);

 protected:
  void SetupUi(QWidget *header_widget, QWidget *content_widget);
  void SetContentHeight(int height);

  QPushButton *expand_button_;
  QWidget *header_widget_;
  QWidget *content_widget_;
  QPropertyAnimation *animation_;
};

#endif  // EXPANDABLE_WIDGET_H_