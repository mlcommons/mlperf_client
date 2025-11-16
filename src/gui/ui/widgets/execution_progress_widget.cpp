#include "execution_progress_widget.h"

#include <QScrollArea>

#include "core/gui_utils.h"

#ifdef Q_OS_IOS
#include <QScroller>
#endif

ExecutionProgressWidget::ExecutionProgressWidget(QWidget *parent)
    : QWidget(parent) {
  setup_ui();
  setup_connections();
}

void ExecutionProgressWidget::setup_ui() {
  setAttribute(Qt::WA_StyledBackground, true);
  setProperty("class", "panel_widget");
  auto main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(5, 5, 5, 5);
  main_layout->setSpacing(10);
  main_layout->addSpacing(10);

#if defined(Q_OS_WIN) || (defined(Q_OS_MACOS))
  m_hide_counters_button = new ToggleButton(true, this);
  m_hide_counters_label = new QLabel("Hide Counters", this);
  m_hide_counters_label->setProperty("class", "medium_normal_label");
  QHBoxLayout *hide_counters_layout = new QHBoxLayout();
  hide_counters_layout->addStretch();
  hide_counters_layout->addWidget(m_hide_counters_button);
  hide_counters_layout->addWidget(m_hide_counters_label);
  hide_counters_layout->addStretch();

  main_layout->addLayout(hide_counters_layout);
#else
  m_hide_counters_button = nullptr;
#endif
  main_layout->addSpacing(5);

  // Progress bar
  m_progress_bar = new CircularProgressBar(232, this);
  m_progress_bar->SetProgress(0);
  m_progress_bar->SetCenterTextVisible(true);
  m_progress_bar->SetBottomTextVisible(true);
  m_progress_bar->SetAddInnerCircle(true);
  m_progress_bar->SetSymbol("%");
  m_progress_bar->SetIconPath(gui::utils::GetDeviceIcon("GPU"));
  main_layout->addWidget(m_progress_bar, 0, Qt::AlignCenter);
  main_layout->addSpacing(10);
  auto eps_frame = new QFrame(this);
  eps_frame->setObjectName("eps_frame");
  eps_frame->setFrameShape(QFrame::NoFrame);
  m_ep_layout = new QVBoxLayout(eps_frame);
  m_ep_layout->setContentsMargins(0, 0, 0, 0);
  m_ep_layout->setSpacing(10);
  m_ep_layout->addStretch();

  QScrollArea *eps_scroll_area = new QScrollArea();
  eps_scroll_area->setWidgetResizable(true);
  eps_scroll_area->setWidget(eps_frame);

  main_layout->addWidget(eps_scroll_area, 1);
  main_layout->addSpacing(10);
  QHBoxLayout *alert_layout = new QHBoxLayout();
  alert_layout->addStretch();

  m_alert_icon = new QLabel(this);
  m_alert_icon->setPixmap(QPixmap(":/icons/resources/icons/ri_alert-fill.png"));
  m_alert_icon->setAlignment(Qt::AlignCenter);
  m_alert_icon->setFixedSize(28, 28);
  alert_layout->addWidget(m_alert_icon);

  m_alert_label = new QLabel(this);
  m_alert_label->setAlignment(Qt::AlignCenter);
  m_alert_label->setProperty("class", "alert_label");

  alert_layout->addWidget(m_alert_label);
  alert_layout->addStretch();
  main_layout->addLayout(alert_layout);
  main_layout->addSpacing(5);
  m_cancel_button = new QPushButton("Cancel", this);
  m_cancel_button->setFixedSize(195, 40);
  m_cancel_button->setProperty("class", "secondary_button");
  main_layout->addWidget(m_cancel_button, 0, Qt::AlignCenter);
  main_layout->addSpacing(25);
  setLayout(main_layout);

  SetCancelingState(false);

#ifdef Q_OS_IOS
  QScroller::grabGesture(eps_scroll_area);
#endif
}

void ExecutionProgressWidget::setup_connections() {
  if (m_hide_counters_button)
    connect(m_hide_counters_button, &ToggleButton::toggled, this,
            &ExecutionProgressWidget::HideCountersToggled);
  connect(m_cancel_button, &QPushButton::clicked, this,
          &ExecutionProgressWidget::CancelClicked);
}

void ExecutionProgressWidget::SetEpSelected(int index) {
  current_selected_ep = index;
  m_progress_bar->SetBottomText(m_ep_widgets[current_selected_ep]->GetName());
  m_progress_bar->SetIconPath(m_ep_widgets[current_selected_ep]->GetIconPath());
  m_progress_bar->SetProgress(0);
  m_ep_widgets[current_selected_ep]->Start();
}

int ExecutionProgressWidget::GetCurrentSelectedEP() const {
  return current_selected_ep;
}

QString ExecutionProgressWidget::GetEPDisplayName(int index) const {
  return m_ep_widgets.at(index)->GetLongName();
}

int ExecutionProgressWidget::GetTotalEPs() const { return m_ep_widgets.size(); }

void ExecutionProgressWidget::SetSelectedEpProgress(int total_steps,
                                                    int current_step) {
  m_ep_widgets.at(current_selected_ep)->SetProgress(total_steps, current_step);
}

int ExecutionProgressWidget::AddEPProgressWidget(const QString &name,
                                                 const QString &description,
                                                 const QString &icon_path,
                                                 const QString &long_name,
                                                 const QString &model_name) {
  auto ep_widget = new EPProgressWidget(name, description, icon_path, long_name,
                                        model_name, this);
  m_ep_widgets.append(ep_widget);
  m_ep_layout->insertWidget(m_ep_layout->count() - 1, ep_widget);
  ep_widget->SetTransparent(m_ep_widgets.size() % 2 == 0);
  ep_widget->update();
  return m_ep_widgets.size() - 1;
}

void ExecutionProgressWidget::MoveToNextEP(bool current_success) {
  if (current_selected_ep != -1) {
    m_ep_widgets[current_selected_ep]->SetCompleted(current_success);
  }
  current_selected_ep++;
  if (current_selected_ep >= m_ep_widgets.size()) {
    current_selected_ep = 0;
  }
  SetEpSelected(current_selected_ep);
}

void ExecutionProgressWidget::SetProgress(int value) {
  if (current_selected_ep == -1) {
    return;
  }
  m_progress_bar->SetProgress(value);
}

void ExecutionProgressWidget::ClearEPs() {
  for (auto ep_widget : m_ep_widgets) {
    m_ep_layout->removeWidget(ep_widget);
    ep_widget->deleteLater();
  }
  m_ep_widgets.clear();
  current_selected_ep = -1;
}

void ExecutionProgressWidget::AddEP(const QString &name,
                                    const QString &description,
                                    const QString &device_type,
                                    const QString &long_name,
                                    const QString &model_name) {
  AddEPProgressWidget(name, description, gui::utils::GetDeviceIcon(device_type),
                      long_name, model_name);
}

void ExecutionProgressWidget::SetTaskName(const QString &name) {
  // check if not in canceling state
  if (m_cancel_button->isEnabled()) m_progress_bar->SetBottomText(name);
}

void ExecutionProgressWidget::SetCancelingState(bool on) {
  m_cancel_button->setEnabled(!on);
  m_alert_label->setText(on ? "Canceling. Please wait."
                            : "Do not close the app");
  if (on) m_progress_bar->SetBottomText("Canceling");
}
