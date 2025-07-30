#include "main_window.h"

#include <QGraphicsBlurEffect>
#include <QKeySequence>
#include <QMouseEvent>
#include <QShortcut>

#include "../CLI/version.h"
// include pages
#include "ui/eula_page.h"
#include "ui/realtime_monitoring_page.h"
#include "ui/results_history_page.h"
#include "ui/results_report_page.h"
#include "ui/settings_page.h"
#include "ui/start_page.h"
#include "ui/widgets/popup_widget.h"

// include controllers
#include "controllers/app_controller.h"
#include "controllers/realtime_page_controller.h"
#include "controllers/results_history_page_controller.h"
#include "controllers/results_report_page_controller.h"
#include "controllers/settings_page_controller.h"
#include "controllers/start_page_controller.h"

namespace gui {
MainWindow::MainWindow(controllers::AppController *app_controller,
                       QWidget *parent)
    : QMainWindow(parent),
      current_page_(PageType::kEulaPage),
      benchmark_tab_last_page_(PageType::kStartPage),
      history_tab_last_page_(PageType::kHistoryPage),
      eula_page_widget_(nullptr),
      start_page_widget_(nullptr),
      realtime_monitoring_page_widget_(nullptr),
      history_page_widget_(nullptr),
      report_page_widget_(nullptr),
      settings_page_widget_(nullptr),
      app_controller_(app_controller),
      is_mouse_pressed_(false) {
  SetupUi();
  CreateActions();
  InitializeControllers();

  InstallSignalHandlers();
}

void MainWindow::SetupUi() {
  ui_.setupUi(this);
  setAttribute(Qt::WA_TranslucentBackground);
  setWindowTitle(QString("MLPerf Client ") + APP_VERSION_STRING " " +
                 APP_BUILD_NAME + " Beta GUI");
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                 Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                 Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
  ui_.m_app_content_frame->setProperty("class", "main_content_frame");
  ui_.m_top_window_bar->setProperty("class", "window_frame_bar");

  ui_.m_test_tab_button_->setEnabled(false);
  ui_.m_history_tab_button_->setEnabled(false);
  ui_.m_settings_tab_button_->setEnabled(false);

  // Create Pages
  // Create the eula page
  eula_page_widget_ = new views::EULAWidget(ui_.m_stacked_widget);
  // Create the start page
  start_page_widget_ = new views::StartPage("", ui_.m_stacked_widget);
  // Create realtime monitoring page
  realtime_monitoring_page_widget_ =
      new views::RealTimeMonitoringPage(ui_.m_stacked_widget);
  // create results history page
  history_page_widget_ = new views::ResultsHistoryPage(ui_.m_stacked_widget);
  // create results report page
  report_page_widget_ = new views::ResultsReportPage(ui_.m_stacked_widget);
  // create settings page
  settings_page_widget_ = new views::SettingsPage(ui_.m_stacked_widget);

  // add pages to the stacked widget in the order they should be shown
  ui_.m_stacked_widget->addWidget(eula_page_widget_);
  ui_.m_stacked_widget->addWidget(start_page_widget_);
  ui_.m_stacked_widget->addWidget(realtime_monitoring_page_widget_);
  ui_.m_stacked_widget->addWidget(history_page_widget_);
  ui_.m_stacked_widget->addWidget(report_page_widget_);
  ui_.m_stacked_widget->addWidget(settings_page_widget_);

  popup_widget_ = new PopupWidget(this);
  popup_widget_->hide();
  blur_effect_ = new QGraphicsBlurEffect(this);
  blur_effect_->setBlurRadius(8);
  blur_effect_->setEnabled(false);
  this->setGraphicsEffect(blur_effect_);
}

void MainWindow::CreateActions() {
  close_app_shortcut_ = new QShortcut(QKeySequence("Ctrl+Q"), this);
}

void MainWindow::InitializeControllers() {
  // Set the view for the controllers
  start_page_widget_->SetController(app_controller_->GetStartPageController());
  app_controller_->GetStartPageController()->SetView(start_page_widget_);

  realtime_monitoring_page_widget_->SetController(
      app_controller_->GetRealtimePageController());
  app_controller_->GetRealtimePageController()->SetView(
      realtime_monitoring_page_widget_);

  history_page_widget_->SetController(
      app_controller_->GetResultsHistoryPageController());
  app_controller_->GetResultsHistoryPageController()->SetView(
      history_page_widget_);

  app_controller_->GetResultsReportPageController()->SetView(
      report_page_widget_);

  app_controller_->GetSettingsPageController()->SetView(settings_page_widget_);
}

void MainWindow::InstallSignalHandlers() {
  connect(close_app_shortcut_, &QShortcut::activated, [this] { close(); });

  // handle SwitchToPage requests from the App Controller
  connect(app_controller_, &controllers::AppController::SwitchToPage, this,
          &MainWindow::SwitchToPage);
  connect(app_controller_, &controllers::AppController::ShowGlobalPopup, this,
          &MainWindow::ShowGlobalPopup, Qt::QueuedConnection);
  connect(app_controller_, &controllers::AppController::HidePopup, this,
          &MainWindow::HidePopup);

  // since the eula page does not have controllers, we need to redirect signals
  // to the App Controller
  connect(eula_page_widget_, &views::EULAWidget::EulaAccepted, app_controller_,
          &controllers::AppController::EulaAccepted);

  // Handle tab bar functionality
  connect(ui_.m_test_tab_button_, &TabBarButton::toggled, this,
          [this](bool checked) {
            if (checked) {
              ui_.m_history_tab_button_->setChecked(false);
              ui_.m_settings_tab_button_->setChecked(false);
              SwitchToPage(benchmark_tab_last_page_);
            }
          });
  connect(ui_.m_history_tab_button_, &TabBarButton::toggled, this,
          [this](bool checked) {
            if (checked) {
              ui_.m_test_tab_button_->setChecked(false);
              ui_.m_settings_tab_button_->setChecked(false);
              SwitchToPage(history_tab_last_page_);
            }
          });
  connect(ui_.m_settings_tab_button_, &TabBarButton::toggled, this,
          [this](bool checked) {
            if (checked) {
              ui_.m_test_tab_button_->setChecked(false);
              ui_.m_history_tab_button_->setChecked(false);
              SwitchToPage(PageType::kSettingsPage);
            }
          });

  connect(popup_widget_, &PopupWidget::rejected, this, &MainWindow::HidePopup);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && ui_.m_top_window_bar->underMouse()) {
    is_mouse_pressed_ = true;
    mouse_press_position_ = event->globalPos() - this->pos();
    event->accept();
  }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
  if (is_mouse_pressed_ && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPos() - mouse_press_position_);
    event->accept();
  }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
  is_mouse_pressed_ = false;
}

void MainWindow::SwitchToPage(PageType page_type) {
  if (page_type == current_page_) return;
  if (CanLeaveCurrentPage()) {
    switch (page_type) {
      case PageType::kEulaPage:
        ui_.m_stacked_widget->setCurrentWidget(eula_page_widget_);
        break;
      case PageType::kStartPage:
        ui_.m_stacked_widget->setCurrentWidget(start_page_widget_);
        break;
      case PageType::kRealTimeMonitoringPage:
        ui_.m_stacked_widget->setCurrentWidget(
            realtime_monitoring_page_widget_);
        break;
      case PageType::kHistoryPage:
        ui_.m_stacked_widget->setCurrentWidget(history_page_widget_);
        break;
      case PageType::kReportPage:
        ui_.m_stacked_widget->setCurrentWidget(report_page_widget_);
        break;
      case PageType::kSettingsPage:
        ui_.m_stacked_widget->setCurrentWidget(settings_page_widget_);
        break;
      default:
        break;
    }
    switch (page_type) {
      case PageType::kEulaPage:
      case PageType::kStartPage:
      case PageType::kRealTimeMonitoringPage:
        benchmark_tab_last_page_ = page_type;
        ui_.m_test_tab_button_->setChecked(true);
        break;
      case PageType::kHistoryPage:
      case PageType::kReportPage:
        history_tab_last_page_ = page_type;
        ui_.m_history_tab_button_->setChecked(true);
        break;
      case PageType::kSettingsPage:
        ui_.m_settings_tab_button_->setChecked(true);
        break;
      default:
        break;
    }
    bool is_eula_page = page_type == PageType::kEulaPage;
    bool is_realtime_monitoring_page =
        page_type == PageType::kRealTimeMonitoringPage;
    ui_.m_test_tab_button_->setEnabled(!is_eula_page);
    ui_.m_history_tab_button_->setEnabled(!is_realtime_monitoring_page &&
                                          !is_eula_page);
    ui_.m_settings_tab_button_->setEnabled(!is_realtime_monitoring_page &&
                                           !is_eula_page);
  }
  current_page_ = page_type;
}

bool MainWindow::CanLeaveCurrentPage() {
  views::AbstractView *current_view = qobject_cast<views::AbstractView *>(
      ui_.m_stacked_widget->currentWidget());
  if (current_view) {
    return current_view->CanExitPage();
  }
  return true;
}

void MainWindow::ShowGlobalPopup(const QString &message) {
  blur_effect_->setEnabled(true);

  popup_widget_->SetMessage(message);
  popup_widget_->move(
      geometry().center() -
      QPoint(popup_widget_->width() / 2, popup_widget_->height() / 2));
  popup_widget_->show();
}

void MainWindow::HidePopup() {
  popup_widget_->hide();
  blur_effect_->setEnabled(false);
}

}  // namespace gui
