#include "system_monitoring_widget.h"

#include <QGridLayout>

#include "capacity_widget_bar.h"
#include "circular_gauge_widget.h"
#include "power_mode_widget.h"
#include "result_component_wrapper_widget.h"
#include "thermometer_widget.h"

SystemMonitoringWidget::SystemMonitoringWidget(QWidget *parent)
    : QWidget{parent}, initialized_(false) {
  main_layout_ = new QGridLayout(this);
  main_layout_->setSpacing(10);
  main_layout_->setContentsMargins(0, 0, 0, 0);
  setLayout(main_layout_);
}

void SystemMonitoringWidget::InitMonitoringWidgets(
    const QList<MonitoringWidgetConfig> &configs) {
  while (QLayoutItem *item = main_layout_->takeAt(0)) delete item;
  for (auto widget : monitoring_widgets_) widget->deleteLater();
  monitoring_widgets_.clear();

  const int cols = 3;
  int current_row = 0, current_col = 0;
  for (const auto &config : configs) {
    QWidget *widget = nullptr, *wrapper = nullptr;
    switch (config.type) {
      case kCircularGauge: {
        auto gauge = new CircularGauge(config.label, QIcon(config.icon_path),
                                       config.description, this);
        widget = gauge;
        wrapper = gauge->wrapper();
        break;
      }
      case kCapacityBar: {
        auto capacity_bar = new CapacityWidgetBar(
            config.label, config.description, config.unit, this);
        capacity_bar->SetMaximum(config.max_value);
        widget = capacity_bar;
        wrapper = capacity_bar->wrapper();
        break;
      }
      case kThermometer: {
        auto thermometer = new ThermometerWidget(
            config.label, QIcon(config.icon_path), config.description, this);
        widget = thermometer;
        wrapper = thermometer->wrapper();
        break;
      }
      case kPowerMode: {
        auto power_mode = new PowerModeWidget(
            config.label, QIcon(config.icon_path), config.description, this);
        widget = power_mode;
        wrapper = power_mode->wrapper();
        break;
      }
      default: {
        continue;
      }
    }
    monitoring_widgets_.append(widget);
    main_layout_->addWidget(wrapper, current_row, current_col);
    ++current_col;
    if (current_col >= cols) {
      current_col = 0;
      ++current_row;
    }
  }
  initialized_ = true;
}

void SystemMonitoringWidget::SetWidgetValue(int index, double value) {
  if (index < 0 || index >= monitoring_widgets_.size()) return;
  auto gauge = dynamic_cast<CircularGauge *>(monitoring_widgets_[index]);
  if (gauge) {
    gauge->SetValue(value);
    return;
  }
  auto capacity_bar =
      dynamic_cast<CapacityWidgetBar *>(monitoring_widgets_[index]);
  if (capacity_bar) {
    capacity_bar->SetValue(value);
    return;
  }
  auto thermometer =
      dynamic_cast<ThermometerWidget *>(monitoring_widgets_[index]);
  if (thermometer) thermometer->SetTemperature(value);
}

void SystemMonitoringWidget::SetWidgetValue(int index, const QString &value) {
  if (index < 0 || index >= monitoring_widgets_.size()) return;
  auto power_mode = dynamic_cast<PowerModeWidget *>(monitoring_widgets_[index]);
  if (power_mode) power_mode->SetMode(value);
}

void SystemMonitoringWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  emit VisibilityChanged(true);
}

void SystemMonitoringWidget::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  emit VisibilityChanged(false);
}
