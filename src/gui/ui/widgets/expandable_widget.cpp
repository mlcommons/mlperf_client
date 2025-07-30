#include "expandable_widget.h"

#include <QLayout>
#include <QPropertyAnimation>
#include <QPushButton>

ExpandableWidget::ExpandableWidget(QWidget *parent)
    : QWidget(parent),
      expand_button_(nullptr),
      header_widget_(nullptr),
      content_widget_(nullptr),
      animation_(nullptr) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAttribute(Qt::WA_StyledBackground, true);
}

void ExpandableWidget::SetupUi(QWidget *header_widget,
                               QWidget *content_widget) {
  header_widget_ = header_widget;
  content_widget_ = content_widget;

  Q_ASSERT(header_widget_);
  Q_ASSERT(content_widget_);

  expand_button_ = new QPushButton(this);
  expand_button_->setCheckable(true);
  expand_button_->setFixedSize(24, 24);
  expand_button_->setCursor(Qt::PointingHandCursor);
  expand_button_->setChecked(false);
  expand_button_->setObjectName("expand_icon_btn");
  QIcon icon;
  icon.addFile(":/icons/resources/icons/expand_left.png", QSize(),
               QIcon::Normal, QIcon::Off);
  icon.addFile(":/icons/resources/icons/expand_down.png", QSize(),
               QIcon::Normal, QIcon::On);
  expand_button_->setIcon(icon);
  expand_button_->setIconSize(expand_button_->size());

  QHBoxLayout *top_layout = new QHBoxLayout;
  top_layout->setContentsMargins(0, 0, 18, 0);
  top_layout->setSpacing(0);
  top_layout->addWidget(header_widget_);
  top_layout->addWidget(expand_button_);

  QWidget *top_widget = new QWidget(this);
  top_widget->setLayout(top_layout);
  if (header_widget_->height())
    top_widget->setFixedHeight(header_widget_->height());

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(top_widget);
  layout->addWidget(content_widget_);
  layout->addStretch();
  setLayout(layout);

  // setup the animation
  SetContentHeight(0);
  animation_ = new QPropertyAnimation(this, "contentHeight");
  animation_->setDuration(250);
  animation_->setStartValue(0);

  connect(expand_button_, &QPushButton::clicked, this,
          &ExpandableWidget::ToggleExpand);
}

void ExpandableWidget::SetContentHeight(int height) {
  content_widget_->setMinimumHeight(height);
  content_widget_->setMaximumHeight(height);
}

void ExpandableWidget::ToggleExpand(bool is_expanded) {
  animation_->setEndValue(content_widget_->sizeHint().height());
  animation_->setDirection(is_expanded ? QAbstractAnimation::Forward
                                       : QAbstractAnimation::Backward);
  animation_->start();
}

void ExpandableWidget::Expand() { expand_button_->click(); }