#ifndef CIRCULAR_GAUGE_WIDGET_H
#define CIRCULAR_GAUGE_WIDGET_H

#include <QIcon>
#include <QSvgRenderer>
#include <QWidget>

class ResultComponentWrapperWidget;

class CircularGauge : public QWidget {
  Q_OBJECT

 public:
  explicit CircularGauge(const QString &title, const QIcon &icon,
                         const QString tooltip_text, QWidget *parent = nullptr);
  void SetValue(double value);
  ResultComponentWrapperWidget *wrapper() const { return wrapper_widget_; }

 protected:
  void paintEvent(QPaintEvent *event) override;

 private:
  double m_value;
  ResultComponentWrapperWidget *wrapper_widget_;
  QSvgRenderer m_svg_renderer;
  void setup_ui();
};

#endif  // CIRCULAR_GAUGE_WIDGET_H
