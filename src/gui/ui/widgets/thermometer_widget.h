#ifndef CIRCULAR_THERMOMETER_H_
#define CIRCULAR_THERMOMETER_H_

#include <QWidget>
class ResultComponentWrapperWidget;

class ThermometerWidget : public QWidget {
  Q_OBJECT

 public:
  explicit ThermometerWidget(const QString &title, const QIcon &icon,
                             const QString tooltip_text,
                             QWidget *parent = nullptr);

  void SetRange(double min_temp, double max_temp);
  void SetTemperature(double temperature);
  ResultComponentWrapperWidget *wrapper() const { return wrapper_widget_; }

 protected:
  void paintEvent(QPaintEvent *event) override;

 private:
  double min_temperature_;
  double max_temperature_;
  double current_temperature_;
  ResultComponentWrapperWidget *wrapper_widget_;
};

#endif
