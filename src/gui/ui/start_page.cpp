#include "start_page.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef Q_OS_IOS
#include <QScroller>
#endif

#include "../core/types.h"
#include "widgets/ep_exapandable_widget.h"
#include "widgets/ep_filters_widget.h"
#include "widgets/information_card_widget.h"

namespace gui {
namespace views {
StartPage::StartPage(QString schema_file_path, QWidget* parent)
    : AbstractView(parent), eps_filters_widget_(nullptr) {}

void StartPage::SetupUi() {
  ui_.setupUi(this);

  ui_.ep_widgets_container->setAttribute(Qt::WA_StyledBackground, true);
  ui_.ep_widgets_container->setProperty("class", "transparent_panel_widget");
  ui_.select_configs_label->setProperty("class", "large_strong_label");
  ui_.select_all_box->setProperty("class", "secondary_checkbox");
  ui_.select_all_label->setProperty("class", "medium_normal_label");
  ui_.select_all_number_label->setProperty("class", "medium_normal_label");
  ui_.show_only_runnable_label->setProperty("class", "medium_normal_label");
  ui_.disclaimer_text_label->setProperty("class", "medium_light_label");

  ui_.show_only_runnable_switch->SetFixedSize(25, 16, 10);
  ui_.m_start_btn->setProperty("class", "primary_button");
  ui_.m_download_only_btn->setProperty("class", "primary_link_button");
  ui_.m_download_only_btn->setCursor(Qt::PointingHandCursor);

#ifdef Q_OS_IOS
  QScroller::grabGesture(ui_.ep_scroll_area);
#endif
}

void StartPage::InstallSignalHandlers() {
  connect(ui_.m_start_btn, &QPushButton::clicked, this,
          &StartPage::StartBenchmarkClicked);
  connect(ui_.m_download_only_btn, &QPushButton::clicked, this,
          &StartPage::DownloadOnlyClicked);
}

void StartPage::OnEPAddButtonClicked() {
  auto sender_object = sender();
  for (int i = 0; i < eps_widgets_.size(); ++i)
    if (eps_widgets_[i] == sender_object) emit AddEPRequested(i);
}

void StartPage::OnEPDeleteButtonClicked() {
  auto sender_object = sender();
  for (int i = 0; i < eps_widgets_.size(); ++i)
    if (eps_widgets_[i] == sender_object) emit DeleteEPRequested(i);
}

void StartPage::UpdateUI() {
  int selected_eps_count = 0;
  int runnable_visible_eps_count = 0;
  for (auto ep_widget : eps_widgets_) {
    if (ep_widget->IsSelected()) ++selected_eps_count;
    if (ep_widget->isVisible() && ep_widget->isEnabled())
      ++runnable_visible_eps_count;
  }

  ui_.m_start_btn->setEnabled(selected_eps_count);
  ui_.m_download_only_btn->setEnabled(selected_eps_count);

  ui_.select_all_box->blockSignals(true);
  ui_.select_all_box->setChecked(runnable_visible_eps_count ==
                                 selected_eps_count);
  ui_.select_all_number_label->setText(
      QString("(%1)").arg(runnable_visible_eps_count));
  ui_.select_all_box->blockSignals(false);

  QString start_button_text =
      QString("Run Benchmark Tests (%1)").arg(selected_eps_count);
  ui_.m_start_btn->setText(start_button_text);
}

void StartPage::AddSystemInformationCard(
    const QString& image_path, const QString& header_text,
    const QList<QPair<QString, QString>>& header_key_values) {
  ui_.system_information_widget->AddSystemInformationCard(
      image_path, header_text, header_key_values);
}

void StartPage::ExpandSystemInformationWidget() {
  ui_.system_information_widget->Expand(false);
}

void StartPage::LoadFiltersCard(const QList<EPFilter>& filters) {
  if (eps_filters_widget_) {
    ui_.ep_filters_container_layout->removeWidget(eps_filters_widget_);
    eps_filters_widget_->deleteLater();
  }

  eps_filters_widget_ = new EPFiltersWidget(filters, ui_.ep_widgets_container);
  ui_.ep_filters_container_layout->addWidget(eps_filters_widget_);

  connect(eps_filters_widget_, &EPFiltersWidget::FilterChanged, this,
          &StartPage::EPsFilterChanged);
  // the "Show only runnable" switch also is part of the filtering
  connect(ui_.show_only_runnable_switch, &QAbstractButton::toggled, this,
          &StartPage::EPsFilterChanged);
  connect(ui_.select_all_box, &QCheckBox::toggled, this,
          &StartPage::SelectAllToggled);
}

void StartPage::LoadEPInformationCards(
    const nlohmann::json& schema, const QList<EPInformationCard>& configs) {
  ClearEPInformationCards();
  for (int i = 0; i < configs.size(); ++i)
    AddEpInformationCard(schema, configs.at(i), i);

  ui_.eps_v_layout->addStretch();

  // Adding this call to the end of event loop, to make sure that widgets
  // initial sizes are already calculated
  QMetaObject::invokeMethod(
      this,
      [this]() {
        int ep_name_max_width = 0;
        for (auto ep_widget : eps_widgets_) {
          int ep_name_width = ep_widget->GetEPNameWidthHint();
          if (ep_name_width > ep_name_max_width)
            ep_name_max_width = ep_name_width;
        }
        if (ep_name_max_width > 0)
          for (auto ep_widget : eps_widgets_)
            ep_widget->SetEPNameWidth(ep_name_max_width);
      },
      Qt::QueuedConnection);
}

void StartPage::ClearEPInformationCards() {
  for (auto widget : eps_widgets_) {
    ui_.eps_v_layout->removeWidget(widget);
    widget->deleteLater();
  }
  eps_widgets_.clear();
  eps_widgets_base_ids.clear();
}

void StartPage::AddEpInformationCard(const nlohmann::json& schema,
                                     const EPInformationCard& ep_config,
                                     int base_id, int index, bool deletable) {
  // check if name is in the schema keys
  nlohmann::json fields = nlohmann::json::object();
  if (schema.contains(ep_config.name_.toStdString())) {
    fields = schema[ep_config.name_.toStdString()];
  } else {
    qDebug() << "Error: " << ep_config.name_ << " not found in schema.";
  }

  QString category = ep_config.config_category_.isEmpty()
                         ? "base"
                         : ep_config.config_category_;
  auto ep_widget = new EPExpandableWidget(
      ep_config.model_name_ + " (" + ep_config.long_name_ + ")",
      ep_config.description_, ep_config.description_, category,
      ep_config.devices_, fields, ep_config.config_, ui_.m_scroll_area_widget);
  ep_widget->SetDeletable(deletable);
  connect(ep_widget, &EPExpandableWidget::AddButtonClicked, this,
          &StartPage::OnEPAddButtonClicked);
  connect(ep_widget, &EPExpandableWidget::DeleteButtonClicked, this,
          &StartPage::OnEPDeleteButtonClicked);
  connect(ep_widget, &EPExpandableWidget::SelectionChanged, this,
          &StartPage::UpdateUI);

  eps_widgets_base_ids[ep_widget] = base_id;
  if (base_id >= 0 && base_id < eps_widgets_.size())
    ep_widget->SetEPNameWidth(eps_widgets_[base_id]->GetEPNameWidth());

  if (index == -1) {
    eps_widgets_.append(ep_widget);
    ui_.eps_v_layout->addWidget(ep_widget);
  } else {
    eps_widgets_.insert(index, ep_widget);
    ui_.eps_v_layout->insertWidget(index, ep_widget);
  }

  UpdateUI();
}

void StartPage::DeleteEpInformationCard(int index) {
  ui_.eps_v_layout->removeWidget(eps_widgets_.at(index));
  eps_widgets_base_ids.remove(eps_widgets_.at(index));

  eps_widgets_.at(index)->deleteLater();
  eps_widgets_.remove(index);
  UpdateUI();
}

bool StartPage::IsEPCardVisible(int index) const {
  return eps_widgets_.at(index)->isVisible();
}

bool StartPage::IsEPCardEnabled(int index) const {
  return eps_widgets_.at(index)->isEnabled();
}

void StartPage::SetEPCardVisible(int index, bool visible) {
  eps_widgets_.at(index)->setVisible(visible);
}

bool StartPage::IsEPCardSelected(int index) const {
  return eps_widgets_.at(index)->IsSelected();
}

QString StartPage::GetEPCardPromptsType(int index) const {
  return eps_widgets_.at(index)->GetPromptsType();
}

void StartPage::SetEPCardSelected(int index, bool selected) {
  eps_widgets_.at(index)->SetSelected(selected);
}

int StartPage::GetEPCardBaseId(int index) const {
  auto widget = eps_widgets_.at(index);
  return eps_widgets_base_ids.value(widget, -1);
}

nlohmann::json StartPage::GetEPConfiguration(int index) const {
  auto widget = eps_widgets_.at(index);
  return widget->GetConfiguration();
}

QList<EPFilter> StartPage::GetEPsFilter() const {
  return eps_filters_widget_->GetFilters();
}

bool StartPage::IsShowOnlyRunnableOn() const {
  return ui_.show_only_runnable_switch->isChecked();
}

}  // namespace views
}  // namespace gui
