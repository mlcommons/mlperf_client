#include "eula_page.h"

namespace gui {
namespace views {
EULAWidget::EULAWidget(QWidget *parent) : AbstractView(parent) {}

void EULAWidget::SetupUi() {
  setObjectName("eula_widget");
  ui_.setupUi(this);

  ui_.m_content_frame->setProperty("class", "panel_widget");
  ui_.accept_eula_checkbox_->setProperty("class", "primary_checkbox");
  ui_.m_accept_eula_text_label_->setProperty("class", "medium_regular_label");
  ui_.continue_button_->setProperty("class", "primary_button");
  ui_.continue_button_->setEnabled(false);

  ui_.license_text_wgt_->LoadPdf(":/resources/MLPerf Client EULA.pdf");
}

void EULAWidget::InstallSignalHandlers() {
  connect(ui_.accept_eula_checkbox_, &QCheckBox::toggled,
          [this](bool checked) { ui_.continue_button_->setEnabled(checked); });
  connect(ui_.continue_button_, &QPushButton::clicked,
          [this] { emit EulaAccepted(); });
}

}  // namespace views
}  // namespace gui
