#include "ep_progress_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

#include "elided_label.h"

EPProgressWidget::EPProgressWidget(const QString &name,
                                   const QString &description,
                                   const QString &icon_path,
                                   const QString &long_name,
                                   const QString &model_name, QWidget *parent)
    : QWidget(parent),
      ep_name_(name),
      ep_description_(description),
      ep_long_name_(long_name),
      model_name_(model_name),
      icon_path_(icon_path) {
  SetupUI();
}

void EPProgressWidget::SetProgress(int total_steps, int current_step) {
  if (total_steps != 0) {
    progress_label_->setText(
        QString("%1/%2").arg(current_step).arg(total_steps));
  }
}

QString EPProgressWidget::GetIconPath() const { return icon_path_; }

void EPProgressWidget::Start() { progress_label_->setText("N/A"); }

void EPProgressWidget::SetCompleted(bool success) {
  progress_label_->setProperty("color", success ? "green" : "red");
  progress_label_->style()->unpolish(progress_label_);
  progress_label_->style()->polish(progress_label_);
}

QString EPProgressWidget::GetName() const { return ep_name_; }

QString EPProgressWidget::GetLongName() const { return ep_long_name_; }

void EPProgressWidget::SetTransparent(bool transparent) {
  if (transparent) {
    this->setProperty("transparent", "true");
  } else {
    this->setProperty("transparent", "false");
  }
  this->style()->unpolish(this);
  this->style()->polish(this);
}

void EPProgressWidget::SetupUI() {
  content_frame_ = new QFrame(this);
  auto content_layout = new QHBoxLayout(content_frame_);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(10);
  auto name_icon_layout = new QGridLayout();
  name_icon_layout->setContentsMargins(0, 0, 0, 0);
  name_icon_layout->setSpacing(4);
  // Icon
  icon_label_ = new QLabel(content_frame_);
  icon_label_->setFixedSize(24, 24);
  icon_label_->setPixmap(QPixmap(icon_path_).scaled(icon_label_->size()));
  icon_label_->setToolTip(ep_description_);
  name_icon_layout->addWidget(icon_label_, 0, 0, 2, 1);

  model_label_ = new QLabel(model_name_, content_frame_);
  model_label_->setProperty("class", "large_strong_label");

  ep_name_label_ = new QLabel(ep_long_name_, content_frame_);
  ep_name_label_->setProperty("class", "small_strong_label");
  auto ep_name_tip_text =
      QString("%1 [%2]").arg(ep_long_name_).arg(ep_description_);
  ep_name_label_->setToolTip(ep_name_tip_text);
  ep_name_label_->setWordWrap(true);

  name_icon_layout->addWidget(model_label_, 0, 1, 1, 1);
  name_icon_layout->addWidget(ep_name_label_, 1, 1, 1, 1);
  content_layout->addLayout(name_icon_layout, 1);

  // Progress Label
  progress_label_ = new QLabel(content_frame_);
  progress_label_->setProperty("class", "large_strong_label");
  progress_label_->setToolTip("Completed Prompts / Total Prompts");
  content_layout->addWidget(progress_label_);

  auto main_layout = new QHBoxLayout(this);
  main_layout->setContentsMargins(10, 2, 10, 2);
  main_layout->setSpacing(0);
  main_layout->addWidget(content_frame_);

  setAttribute(Qt::WA_StyledBackground, true);
  setProperty("transparent", "false");
}