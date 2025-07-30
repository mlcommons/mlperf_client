#include "settings_page.h"

#include <QFileDialog>

namespace gui {
namespace views {
SettingsPage::SettingsPage(QWidget *parent) : AbstractView(parent) {}

void SettingsPage::SetupUi() {
  ui_.setupUi(this);
  ui_.check_for_updates_btn_->setVisible(false);
  ui_.cooldown_wgt_->setVisible(false);

  ui_.settings_title_->setProperty("class", "title");
  ui_.check_for_updates_btn_->setProperty("class",
                                          "secondary_button_with_icon");
  ui_.clear_cache_btn_->setProperty("class", "secondary_button");
  ui_.reset_to_defaults_btn_->setProperty("class", "secondary_button");

  ui_.data_wgt_->setProperty("class", "transparent_panel_widget");
  ui_.log_wgt_->setProperty("class", "transparent_panel_widget");
  ui_.cooldown_wgt_->setProperty("class", "transparent_panel_widget");
  ui_.data_path_label_->setProperty("class", "large_strong_label");
  ui_.logs_path_label_->setProperty("class", "large_strong_label");
  ui_.keep_logs_label_->setProperty("class", "medium_normal_label");
  ui_.cooldown_label_->setProperty("class", "large_strong_label");
  ui_.cooldown_description_label_->setProperty("class", "medium_normal_label");
}

void SettingsPage::InstallSignalHandlers() {
  connect(ui_.check_for_updates_btn_, &QPushButton::clicked, this,
          []() { qInfo() << "Check for updates clicked"; });
  connect(ui_.cooldown_switch_, &ToggleButton::toggled, this,
          [](bool checked) { qInfo() << "Cooldown toggled: " << checked; });

  connect(ui_.cooldown_slider_, &QSlider::valueChanged, this, [](int value) {
    qInfo() << "Cooldown slider value changed: " << value;
  });

  connect(ui_.data_path_box_, &PathComboBox::PathChanged, this,
          &SettingsPage::DataPathChanged);
  connect(ui_.logs_path_box_, &PathComboBox::PathChanged, this,
          &SettingsPage::LogsPathChanged);
  connect(ui_.log_switch_, &ToggleButton::toggled, this,
          &SettingsPage::KeepLogsChanged);
  connect(ui_.clear_cache_btn_, &QPushButton::clicked, this,
          &SettingsPage::ClearCacheRequested);
  connect(ui_.reset_to_defaults_btn_, &QPushButton::clicked, this,
          &SettingsPage::ResetToDefaultsRequested);
}

void SettingsPage::SetDataPaths(const QStringList &paths) {
  ui_.data_path_box_->SetPredefinedPaths(paths);
}

void SettingsPage::SetLogPaths(const QStringList &paths) {
  ui_.logs_path_box_->SetPredefinedPaths(paths);
}

void SettingsPage::SetDataCurrentPath(const QString &path) {
  ui_.data_path_box_->SetSelectedPath(path);
}

void SettingsPage::SetLogsCurrentPath(const QString &path) {
  ui_.logs_path_box_->SetSelectedPath(path);
}

void SettingsPage::SetKeepLogs(bool keep) { ui_.log_switch_->setChecked(keep); }

QString SettingsPage::GetDataCurrentPath() const {
  return ui_.data_path_box_->GetSelectedPath();
}

QString SettingsPage::GetLogsCurrentPath() const {
  return ui_.logs_path_box_->GetSelectedPath();
}

bool SettingsPage::GetKeepLogs() const { return ui_.log_switch_->isChecked(); }

}  // namespace views
}  // namespace gui
