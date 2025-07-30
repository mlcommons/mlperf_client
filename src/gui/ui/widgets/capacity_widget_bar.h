#ifndef CAPACITY_WIDGET_BAR_H_
#define CAPACITY_WIDGET_BAR_H_

#include <QProgressBar>

class ResultComponentWrapperWidget;

class CapacityWidgetBar : public QProgressBar {
  Q_OBJECT

 public:
  explicit CapacityWidgetBar(const QString &title, const QString tooltip_text,
                             const QString unit, QWidget *parent = nullptr);
  ResultComponentWrapperWidget *wrapper() const { return wrapper_widget_; }
  void SetValue(double value);
  void SetMaximum(double value);
  void SetUnit(const QString &unit);

 public slots:
  void update();

 private:
  QString unit_;
  double max_value_;
  double value_;
  ResultComponentWrapperWidget *wrapper_widget_;
};

#endif  // CAPACITY_WIDGET_BAR_H_
