#include "popup_widget.h"

#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPushButton>

static constexpr int POPUP_WIDGET_WIDTH = 400;
static constexpr int POPUP_WIDGET_HEIGHT = 190;
static constexpr int POPUP_WIDGET_RADIUS = 10;

PopupWidget::PopupWidget(NoIcon, QWidget* parent) : QDialog(parent) {
  setFixedSize(POPUP_WIDGET_WIDTH, POPUP_WIDGET_HEIGHT);
  setWindowFlag(Qt::FramelessWindowHint, true);
  setModal(true);
  setAttribute(Qt::WA_TranslucentBackground);

  close_button_ = new QPushButton(this);
  close_button_->setIcon(QIcon(":/icons/resources/icons/close_icon.png"));
  close_button_->setIconSize(QSize(18, 18));
  close_button_->setFixedSize(24, 24);
  connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);

  message_label_ = new QLabel(this);
  message_label_->setProperty("class", "header_H3");
  message_label_->setAlignment(Qt::AlignCenter);

  main_layout_ = new QVBoxLayout(this);
  main_layout_->setContentsMargins(9, 9, 9, 9);
  main_layout_->setSpacing(0);
  main_layout_->addWidget(close_button_, 0, Qt::AlignRight);
  main_layout_->addStretch();
  main_layout_->addWidget(message_label_, 0, Qt::AlignCenter);
  main_layout_->addSpacing(8);
  main_layout_->addStretch();
}

PopupWidget::PopupWidget(QWidget* parent) : PopupWidget(NoIcon{}, parent) {
  auto* icon_label = new QLabel(this);
  icon_label->setPixmap(
      QIcon(":/icons/resources/icons/popup_info_icon.png").pixmap(56, 56));
  main_layout_->insertWidget(1, icon_label, 0, Qt::AlignCenter);
}

void PopupWidget::SetMessage(const QString& message) {
  message_label_->setText(message);
}

void PopupWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(QColor("#05487E"));
  QPen pen(QColor("#5583A7"), 1);
  painter.setPen(pen);
  painter.drawRoundedRect(rect(), POPUP_WIDGET_RADIUS, POPUP_WIDGET_RADIUS);

  QDialog::paintEvent(event);
}

void PopupWidget::ShowMessage(const QString& message, QWidget* parent) {
  PopupWidget popup(parent);
  popup.SetMessage(message);
  popup.exec();
}

QuestionPopupWidget::QuestionPopupWidget(QWidget* parent,
                                         bool show_do_not_ask_again)
    : PopupWidget(parent) {
  if (show_do_not_ask_again) {
    do_not_ask_again_checkbox_ = new QCheckBox(this);
    do_not_ask_again_checkbox_->setProperty("class", "secondary_checkbox");

    auto* dont_ask_label = new QLabel("Don't ask again after approval", this);
    dont_ask_label->setProperty("class", "medium_regular_label");

    auto* checkbox_layout = new QHBoxLayout();
    checkbox_layout->setSpacing(0);
    checkbox_layout->addStretch();
    checkbox_layout->addWidget(do_not_ask_again_checkbox_);
    checkbox_layout->addSpacing(1);
    checkbox_layout->addWidget(dont_ask_label);
    checkbox_layout->addStretch();

    main_layout_->addLayout(checkbox_layout);
    main_layout_->addSpacing(8);
  }

  auto* yes_button = new QPushButton("Yes", this);
  yes_button->setFixedSize(100, 40);
  yes_button->setProperty("class", "secondary_button");
  connect(yes_button, &QPushButton::clicked, this, &QDialog::accept);

  auto* no_button = new QPushButton("No", this);
  no_button->setFixedSize(100, 40);
  no_button->setProperty("class", "secondary_button");
  connect(no_button, &QPushButton::clicked, this, &QDialog::reject);

  auto* buttons_layout = new QHBoxLayout();
  buttons_layout->addWidget(yes_button);
  buttons_layout->addWidget(no_button);

  main_layout_->addLayout(buttons_layout);
  main_layout_->addStretch();
}

bool QuestionPopupWidget::DoNotAskAgainChecked() const {
  return do_not_ask_again_checkbox_ && do_not_ask_again_checkbox_->isChecked();
}
