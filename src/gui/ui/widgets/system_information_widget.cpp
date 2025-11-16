#include "system_information_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>

#ifdef Q_OS_IOS
#include <QScroller>
#endif

#include "information_card_widget.h"

SystemInformationWidget::SystemInformationWidget(QWidget* parent)
    : ExpandableWidget(parent) {
  setProperty("class", "transparent_panel_widget");

  // header widget
  QLabel* header_label = new QLabel("System Information", this);
  header_label->setProperty("class", "large_strong_label");
  QWidget* header_widget = new QWidget(this);
  QHBoxLayout* header_layout = new QHBoxLayout();
  header_layout->setContentsMargins(24, 0, 0, 0);
  header_layout->addWidget(header_label);
  header_widget->setLayout(header_layout);
  header_widget->setFixedHeight(60);

  // content widget
  QScrollArea* content_widget = new QScrollArea();
  content_widget->setWidgetResizable(true);
  content_widget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  content_widget->verticalScrollBar()->setEnabled(false);

  information_widgets_layout_ = new QHBoxLayout();
  information_widgets_layout_->setContentsMargins(10, 10, 10, 10);
  QWidget* information_widgets_container = new QWidget();
  information_widgets_container->setLayout(information_widgets_layout_);
  content_widget->setWidget(information_widgets_container);

  SetupUi(header_widget, content_widget);

#ifdef Q_OS_IOS
  QScroller::grabGesture(content_widget);
#endif
}

void SystemInformationWidget::AddSystemInformationCard(
    const QString& image_path, const QString& header_text,
    const QList<QPair<QString, QString>>& header_key_values) {
  auto information_card = new InformationCardWidget(
      image_path, header_text, header_key_values, content_widget_);

  information_widgets_layout_->addWidget(information_card);
}