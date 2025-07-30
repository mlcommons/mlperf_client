#ifndef SYSTEM_MONITORING_WIDGET_H
#define SYSTEM_MONITORING_WIDGET_H

#include <QWidget>

class QGridLayout;

class SystemMonitoringWidget : public QWidget {
  Q_OBJECT
 public:
  enum MonitoringWidgetType {
    kCircularGauge,
    kCapacityBar,
    kThermometer,
    kPowerMode,
  };

  struct MonitoringWidgetConfig {
    MonitoringWidgetType type;
    QString label;
    QString icon_path;
    QString description;
    QString unit;
    double max_value;
  };

  explicit SystemMonitoringWidget(QWidget *parent = nullptr);
  void InitMonitoringWidgets(const QList<MonitoringWidgetConfig> &configs);
  bool Initialized() const { return initialized_; }
  void SetWidgetValue(int index, double value);
  void SetWidgetValue(int index, const QString &value);

 signals:
  void VisibilityChanged(bool visible);

 protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

 private:
  QList<QWidget *> monitoring_widgets_;
  QGridLayout *main_layout_;
  bool initialized_;
};

#endif  // SYSTEM_MONITORING_WIDGET_H
