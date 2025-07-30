#ifndef __ABSTRACT_VIEW_H__
#define __ABSTRACT_VIEW_H__
#include <QHBoxLayout>
#include <QWidget>

namespace gui {
namespace controllers {
class AbstractController;
}  // namespace controllers
namespace views {
class AbstractView : public QWidget {
  Q_OBJECT
 public:
  explicit AbstractView(QWidget* parent = nullptr);
  virtual ~AbstractView() = default;

  // Disable copy and assignment
  AbstractView(const AbstractView&) = delete;
  AbstractView& operator=(const AbstractView&) = delete;

  // Add control to the view
  void SetController(gui::controllers::AbstractController* controller);
  gui::controllers::AbstractController* controller() const {
    return controller_;
  }

  // Manage the view lifecycle
  void OnEnterPage();
  void OnExitPage();
  bool CanExitPage();

 protected:
  virtual void SetupUi() = 0;
  virtual void InstallSignalHandlers() = 0;

  void showEvent(QShowEvent* event) override;

 private:
  void InitializeView();
  gui::controllers::AbstractController* controller_;
  bool is_initialized_;
};
}  // namespace views
}  // namespace gui
#endif