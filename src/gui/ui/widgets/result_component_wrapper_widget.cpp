#include "result_component_wrapper_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

ResultComponentWrapperWidget::ResultComponentWrapperWidget(
    QWidget *center_widget, const QString &top_text, const QIcon &icon,
    const QString &bottom_text, const QString tooltip_text, QWidget *parent)
    : QWidget(parent), center_widget_(center_widget) {
  setup_ui(top_text, icon, bottom_text, tooltip_text);
}

void ResultComponentWrapperWidget::setup_ui(const QString &top_text,
                                            const QIcon &icon,
                                            const QString &bottom_text,
                                            const QString tooltip_text) {
  setObjectName("resultComponentWrapperWidget");
  auto *main_layout = new QVBoxLayout(this);
  main_layout->setSpacing(6);
  main_layout->setContentsMargins(3, 6, 6, 3);

  // Top section with icon and text
  auto *top_layout = new QHBoxLayout();
  top_layout->addSpacing(16);
  top_layout->addStretch(1);
  QHBoxLayout *text_icon_layout = new QHBoxLayout();
  text_icon_layout->setContentsMargins(0, 5, 0, 0);

  icon_label_ = new QLabel(this);
  icon_label_->setPixmap(icon.pixmap(24, 24));
  text_label_ = new QLabel(top_text, this);
  text_label_->setObjectName("top_text_label");
  text_label_->setAlignment(Qt::AlignCenter);
  text_icon_layout->addWidget(icon_label_);
  text_icon_layout->addWidget(text_label_);
  text_icon_layout->setAlignment(icon_label_, Qt::AlignCenter);
  text_icon_layout->setAlignment(text_label_, Qt::AlignCenter);
  top_layout->addLayout(text_icon_layout);
  top_layout->addStretch(1);
  top_info_label_ = new QLabel(this);
  top_info_label_->setPixmap(
      QIcon(":/icons/resources/icons/action_info-16.png").pixmap(16, 16));
  top_info_label_->setToolTip(top_text);
  top_info_label_->setToolTip(tooltip_text);

  top_layout->addWidget(top_info_label_);
  top_layout->setAlignment(top_info_label_, Qt::AlignTop | Qt::AlignRight);

  // Center section
  center_layout_ = new QHBoxLayout();
  center_layout_->addWidget(center_widget_);
  center_layout_->setAlignment(Qt::AlignCenter);

  // Bottom text
  bottom_text_label_ = new QLabel(bottom_text, this);
  bottom_text_label_->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
  bottom_text_label_->setObjectName("bottom_text_label");

  main_layout->addLayout(top_layout);
  main_layout->addLayout(center_layout_, 1);
  main_layout->addWidget(bottom_text_label_);

  setLayout(main_layout);
}
