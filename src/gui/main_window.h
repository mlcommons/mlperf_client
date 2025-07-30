#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>

#include "core/types.h"
#include "ui_main_window.h"

class QMouseEvent;
class QShortcut;
class PopupWidget;
class QGraphicsBlurEffect;

namespace gui {
namespace controllers {
class AppController;
}  // namespace controllers

namespace views {
class EULAWidget;
class RunningPage;
class StartPage;
class RealTimeMonitoringPage;
class SettingsPage;
class ResultsHistoryPage;
class ResultsReportPage;

}  // namespace views

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  MainWindow(controllers::AppController* app_controller, QWidget *parent = nullptr);
  bool CanLeaveCurrentPage();

 public slots:
  void SwitchToPage(gui::PageType page_type);
  void ShowGlobalPopup(const QString &message);
  void HidePopup();

 protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

 private:
  Ui::MainWindow ui_;
  views::EULAWidget *eula_page_widget_;
  views::StartPage *start_page_widget_;
  views::RealTimeMonitoringPage *realtime_monitoring_page_widget_;
  views::ResultsHistoryPage *history_page_widget_;
  views::ResultsReportPage *report_page_widget_;
  views::SettingsPage *settings_page_widget_;

  PopupWidget *popup_widget_;
  QGraphicsBlurEffect *blur_effect_;

  controllers::AppController *app_controller_;

  PageType current_page_;
  PageType benchmark_tab_last_page_;
  PageType history_tab_last_page_;

  QShortcut *close_app_shortcut_;
  bool is_mouse_pressed_;
  QPoint mouse_press_position_;

  void SetupUi();
  void CreateActions();
  void InstallSignalHandlers();
  void InitializeControllers();
};
}  // namespace gui
#endif  // MAIN_WINDOW_H
