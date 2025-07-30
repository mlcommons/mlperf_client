#include "information_card_widget.h"

#include <QLabel>
#include <QLayout>
#include <QPixmap>

InformationCardWidget::InformationCardWidget(
    const QString& image_path, const QString& header_text,
    const QList<QPair<QString, QString>>& header_key_values, QWidget* parent)
    : QWidget(parent) {
  setAttribute(Qt::WA_StyledBackground, true);

  QLabel* image_label = new QLabel();
  image_label->setFixedSize(94, 94);
  image_label->setPixmap(QPixmap(image_path)
                             .scaled(image_label->size(), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation));
  image_label->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

  QVBoxLayout* header_layout = new QVBoxLayout();
  header_layout->setSpacing(15);
  QLabel* header_label = new QLabel(header_text);
  header_label->setProperty("class", "header");
  header_layout->addWidget(header_label);
  QLayout* header_key_values_layout = CreateKeyValueLayout(header_key_values);
  header_layout->addLayout(header_key_values_layout);
  header_layout->addStretch();

  QHBoxLayout* main_layout = new QHBoxLayout();
  main_layout->setContentsMargins(15, 15, 10, 10);
  main_layout->setSpacing(10);
  main_layout->addWidget(image_label, 0, Qt::AlignTop);
  main_layout->addLayout(header_layout);

  setLayout(main_layout);
}

QLayout* InformationCardWidget::CreateKeyValueLayout(
    const QList<QPair<QString, QString>>& key_values) {
  QGridLayout* key_value_layout = new QGridLayout();
  key_value_layout->setContentsMargins(5, 0, 0, 0);
  key_value_layout->setVerticalSpacing(10);
  key_value_layout->setHorizontalSpacing(20);
  key_value_layout->setColumnStretch(2, 1);

  int row = 0;
  for (const auto& pair : key_values) {
    QLabel* key_label = new QLabel(pair.first);
    QLabel* value_label = new QLabel(pair.second);
    key_label->setProperty("class", "key");
    value_label->setProperty("class", "value");
    key_value_layout->addWidget(key_label, row, 0);
    key_value_layout->addWidget(value_label, row, 1);
    ++row;
  }

  return key_value_layout;
}
