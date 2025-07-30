#ifndef POWER_MODE_WIDGET_H
#define POWER_MODE_WIDGET_H

#include <QLabel>

class ResultComponentWrapperWidget;

class PowerModeWidget : public QLabel {
  Q_OBJECT
 public:
  explicit PowerModeWidget(const QString &title, const QIcon &icon,
                           const QString tooltip_text,
                           QWidget *parent = nullptr);
  ResultComponentWrapperWidget *wrapper() const { return wrapper_widget_; }
  void SetMode(const QString &mode);

 private:
  ResultComponentWrapperWidget *wrapper_widget_;
};

#endif  // POWER_MODE_WIDGET_H
