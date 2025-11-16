#ifndef SYSTEM_INFORMATION_WIDGET_H_
#define SYSTEM_INFORMATION_WIDGET_H_

#include "expandable_widget.h"

class QHBoxLayout;

/**
 * @class SystemInformationWidget
 * @brief Expandable widget for system hardware information display.
 */
class SystemInformationWidget : public ExpandableWidget {
  Q_OBJECT

 public:
  explicit SystemInformationWidget(QWidget *parent);

  void AddSystemInformationCard(
      const QString& image_path, const QString& header_text,
      const QList<QPair<QString, QString>>& header_key_values);

 private:
  QHBoxLayout* information_widgets_layout_;
};

#endif  // SYSTEM_INFORMATION_WIDGET_H_