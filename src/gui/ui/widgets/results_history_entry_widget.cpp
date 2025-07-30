#include "results_history_entry_widget.h"

#include <QDateTime>
#include <QStyle>

#include "core/types.h"

ResultsHistoryEntryWidget::ResultsHistoryEntryWidget(
    const QString &name, const QDateTime &dateTime, bool passed,
    double time_to_first_token, double token_generation_rate,
    const QString &error_message, const gui::SystemInfoDetails &sys_info,
    QWidget *parent)
    : QWidget(parent), is_passed_(passed) {
  ui_.setupUi(this);

  this->setAttribute(Qt::WA_StyledBackground, true);
  setProperty("class", "history_entry_widget");
  ui_.selection_box_->setProperty("class", "primary_checkbox");
  ui_.ep_label_->setProperty("class", "large_strong_label");
  ui_.time_label_->setProperty("class", "small_normal_label");
  ui_.TTFT_title_label_->setProperty("class", "small_strong_label");
  ui_.TTFT_title_label_->setProperty("semi_transparent", "true");
  ui_.TTFT_label_->setProperty("class", "medium_strong_label");
  ui_.TPS_title_label_->setProperty("class", "small_strong_label");
  ui_.TPS_title_label_->setProperty("semi_transparent", "true");
  ui_.TPS_label_->setProperty("class", "medium_strong_label");
  ui_.error_label_->setProperty("class", "small_normal_label");
  ui_.error_label_->setProperty("semi_transparent", "true");
  ui_.open_button_->setProperty("class", "secondary_button_with_icon");
  ui_.cpu_bullet_->setObjectName("bullet_rounded");
  ui_.gpu_bullet_->setObjectName("bullet_rounded");
  ui_.os_bullet_->setObjectName("bullet_rounded");
  ui_.ram_bullet_->setObjectName("bullet_rounded");
  ui_.gpu_vram_bullet_->setObjectName("bullet_rounded");
  ui_.cpu_title_label_->setObjectName("history_sys_info_key_label");
  ui_.gpu_title_label_->setObjectName("history_sys_info_key_label");
  ui_.os_title_label_->setObjectName("history_sys_info_key_label");
  ui_.ram_title_label_->setObjectName("history_sys_info_key_label");
  ui_.gpu_vram_title_label_->setObjectName("history_sys_info_key_label");

  ui_.cpu_label_->setText(sys_info.cpu_name);
  ui_.cpu_label_->setVisible(!sys_info.cpu_name.isEmpty());
  ui_.cpu_bullet_->setVisible(!sys_info.cpu_name.isEmpty());
  ui_.cpu_title_label_->setVisible(!sys_info.cpu_name.isEmpty());
  ui_.gpu_label_->setText(sys_info.gpu_name);
  ui_.gpu_label_->setVisible(!sys_info.gpu_name.isEmpty());
  ui_.gpu_bullet_->setVisible(!sys_info.gpu_name.isEmpty());
  ui_.gpu_title_label_->setVisible(!sys_info.gpu_name.isEmpty());
  ui_.os_label_->setText(sys_info.os_name);
  ui_.os_label_->setVisible(!sys_info.os_name.isEmpty());
  ui_.os_bullet_->setVisible(!sys_info.os_name.isEmpty());
  ui_.os_title_label_->setVisible(!sys_info.os_name.isEmpty());
  ui_.ram_label_->setText(sys_info.ram);
  ui_.ram_label_->setVisible(!sys_info.ram.isEmpty());
  ui_.ram_bullet_->setVisible(!sys_info.ram.isEmpty());
  ui_.ram_title_label_->setVisible(!sys_info.ram.isEmpty());
  ui_.gpu_vram_label_->setText(sys_info.gpu_ram);
  ui_.gpu_vram_label_->setVisible(!sys_info.gpu_ram.isEmpty());
  ui_.gpu_vram_bullet_->setVisible(!sys_info.gpu_ram.isEmpty());
  ui_.gpu_vram_title_label_->setVisible(!sys_info.gpu_ram.isEmpty());

  ui_.TTFT_icon_label_->setVisible(passed);
  ui_.TTFT_title_label_->setVisible(passed);
  ui_.TTFT_label_->setVisible(passed);
  ui_.TPS_icon_label_->setVisible(passed);
  ui_.TPS_title_label_->setVisible(passed);
  ui_.TPS_label_->setVisible(passed);
  ui_.error_label_->setVisible(!passed);
  ui_.error_icon_label_->setVisible(!passed);

  ui_.ep_label_->setText(name);
  ui_.date_label_->setText(dateTime.toString("MMM dd, yyyy"));
  ui_.time_label_->setText(dateTime.toString("h:mm AP"));

  if (passed) {
    ui_.TTFT_label_->setText(
        time_to_first_token == 0.0
            ? "N/A"
            : QString::number(time_to_first_token, 'f', 2));
    ui_.TPS_label_->setText(
        token_generation_rate == 0.0
            ? "N/A"
            : QString::number(token_generation_rate, 'f', 1));
  } else {
    ui_.error_label_->SetText(error_message);
    ui_.open_button_->setEnabled(false);
  }

  SetSelected(false);

  connect(ui_.open_button_, &QPushButton::clicked, this,
          &ResultsHistoryEntryWidget::OpenButtonClicked);
  connect(ui_.selection_box_, &QCheckBox::toggled, this,
          &ResultsHistoryEntryWidget::SelectionBoxChecked);
}
void ResultsHistoryEntryWidget::SetSelected(bool selected) {
  ui_.selection_box_->blockSignals(true);
  ui_.selection_box_->setChecked(selected);
  ui_.selection_box_->blockSignals(false);
  setProperty("selected", selected);
  style()->polish(this);
}

bool ResultsHistoryEntryWidget::IsPassed() { return is_passed_; }
