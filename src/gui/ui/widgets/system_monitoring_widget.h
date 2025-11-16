#ifndef SYSTEM_MONITORING_WIDGET_H
#define SYSTEM_MONITORING_WIDGET_H

#include <QWidget>

class QGridLayout;

/**
 * @class SystemMonitoringWidget
 * @brief Widget for displaying multiple system monitoring components.
 */
class SystemMonitoringWidget : public QWidget {
  Q_OBJECT

 public:
  /**
   * @brief Types of monitoring widgets available.
   */
  enum MonitoringWidgetType {
    kCircularGauge,
    kCapacityBar,
    kThermometer,
    kPowerMode,
  };

  /**
   * @brief Configuration structure for monitoring widgets.
   */
  struct MonitoringWidgetConfig {
    MonitoringWidgetType type;
    QString label;
    QString icon_path;
    QString description;
    QString unit;
    double max_value;
  };

  explicit SystemMonitoringWidget(QWidget *parent = nullptr);


  /**
   * @brief Initializes monitoring widgets based on configuration.
   */
  void InitMonitoringWidgets(const QList<MonitoringWidgetConfig> &configs);

  /**
   * @brief Checks if the widget has been initialized.
   */
  bool Initialized() const { return initialized_; }

  /**
   * @brief Sets a numeric value for a monitoring widget.
   */
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