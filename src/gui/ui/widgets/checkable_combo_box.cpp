#include "checkable_combo_box.h"

#include <QAbstractItemView>
#include <QListView>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyleFactory>
#include <QStylePainter>

CheckableComboBox::CheckableComboBox(const QList<QPair<QString, bool>>& options,
                                     QWidget* parent)
    : QComboBox(parent), options_(options) {
  model_ = new QStandardItemModel(this);
  setModel(model_);
  setView(new QListView());

#ifdef __APPLE__
  // This makes combobox popup look identical on Windows, macOS, and iOS
  view()->window()->setWindowFlags(view()->window()->windowFlags() |
                                   Qt::NoDropShadowWindowHint);
  setStyle(QStyleFactory::create("Windows"));
#endif

  for (auto& [option_name, is_active] : options_) {
    auto item = new QStandardItem(option_name);
    item->setToolTip(option_name);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setData(is_active ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    model_->appendRow(item);
  }

  connect(view(), &QAbstractItemView::pressed, this,
          &CheckableComboBox::OnIndexPressed);
  connect(model_, &QStandardItemModel::dataChanged, this,
          &CheckableComboBox::OnDataChanged);

  UpdateText();
}

QList<QPair<QString, bool>> CheckableComboBox::GetOptions() const {
  return options_;
}

void CheckableComboBox::OnIndexPressed(const QModelIndex& index) {
  int row = index.row();
  if (row < 0 || row > model_->rowCount()) return;
  if (QStandardItem* item = model_->item(row))
    item->setData(item->data(Qt::CheckStateRole) == Qt::Unchecked
                      ? Qt::Checked
                      : Qt::Unchecked,
                  Qt::CheckStateRole);
}

void CheckableComboBox::OnDataChanged(const QModelIndex& top_left,
                                      const QModelIndex& bottom_right,
                                      const QList<int>&) {
  for (int row = top_left.row(); row <= bottom_right.row(); row++) {
    if (QStandardItem* item = model_->item(row)) {
      options_[row].second = item->checkState() == Qt::Checked;
    }
  }
  UpdateText();
  emit FilterChanged();
}

void CheckableComboBox::UpdateText() {
  QStringList texts;
  for (int i = 0; i < model_->rowCount(); i++)
    if (QStandardItem* item = model_->item(i))
      if (item->checkState() == Qt::Checked) texts << item->text();

  header_text_ = "Select items ...";
  if (options_.size() == texts.size())
    header_text_ = "All";
  else if (!texts.isEmpty())
    header_text_ = texts.join(",");

  setToolTip(header_text_);
  update();
}

void CheckableComboBox::paintEvent(QPaintEvent*) {
  QStylePainter painter(this);
  painter.setPen(palette().color(QPalette::Text));

  QStyleOptionComboBox opt;
  initStyleOption(&opt);
  painter.drawComplexControl(QStyle::CC_ComboBox, opt);

  QRect textRect = style()->subControlRect(QStyle::CC_ComboBox, &opt,
                                           QStyle::SC_ComboBoxEditField, this);
  QFontMetrics fm(font());
  opt.currentText =
      fm.elidedText(header_text_, Qt::ElideRight, textRect.width());

  painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}