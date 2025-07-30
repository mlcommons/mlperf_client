#include "popup_widget.h"

#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

static constexpr int POPUP_WIDGET_WIDTH = 400;
static constexpr int POPUP_WIDGET_HEIGHT = 190;
static constexpr int POPUP_WIDGET_RADIUS = 10;

PopupWidget::PopupWidget(QWidget* parent, bool is_question_popup)
    : QDialog(parent) {
  setFixedSize(POPUP_WIDGET_WIDTH, POPUP_WIDGET_HEIGHT);
  setWindowFlag(Qt::FramelessWindowHint, true);
  setModal(true);
  setAttribute(Qt::WA_TranslucentBackground);

  close_button_ = new QPushButton(this);
  close_button_->setIcon(QIcon(":/icons/resources/icons/close_icon.png"));
  close_button_->setIconSize(QSize(18, 18));
  close_button_->setFixedSize(24, 24);
  connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);

  auto* icon_label = new QLabel(this);
  icon_label->setPixmap(
      QIcon(":/icons/resources/icons/popup_info_icon.png").pixmap(56, 56));

  message_label_ = new QLabel(this);
  message_label_->setProperty("class", "header_H3");
  message_label_->setAlignment(Qt::AlignCenter);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(9, 9, 9, 9);
  main_layout->setSpacing(0);
  main_layout->addWidget(close_button_, 0, Qt::AlignRight);
  main_layout->addWidget(icon_label, 0, Qt::AlignCenter);
  main_layout->addStretch();
  main_layout->addWidget(message_label_, 0, Qt::AlignCenter);

  if (is_question_popup) {
    QPushButton* yes_button = new QPushButton("Yes", this);
    yes_button->setFixedSize(100, 40);
    yes_button->setProperty("class", "secondary_button");
    connect(yes_button, &QPushButton::clicked, this, &QDialog::accept);

    QPushButton* no_button = new QPushButton("No", this);
    no_button->setFixedSize(100, 40);
    no_button->setProperty("class", "secondary_button");
    connect(no_button, &QPushButton::clicked, this, &QDialog::reject);

    QHBoxLayout* main_buttons_layout = new QHBoxLayout();
    main_buttons_layout->addWidget(yes_button);
    main_buttons_layout->addWidget(no_button);

    main_layout->addStretch();
    main_layout->addLayout(main_buttons_layout);
  }

  main_layout->addStretch();
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
