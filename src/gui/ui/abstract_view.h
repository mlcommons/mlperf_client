#ifndef __ABSTRACT_VIEW_H__
#define __ABSTRACT_VIEW_H__
#include <QHBoxLayout>
#include <QWidget>

namespace gui {
namespace controllers {
class AbstractController;
}  // namespace controllers
namespace views {

/**
 * @class AbstractView
 * @brief Base class for all GUI view components.
 */
class AbstractView : public QWidget {
  Q_OBJECT
 public:
  explicit AbstractView(QWidget* parent = nullptr);
  virtual ~AbstractView() = default;

  /**
   * @brief Deleted copy constructor to prevent copying.
   */
  AbstractView(const AbstractView&) = delete;

  /**
   * @brief Deleted assignment operator to prevent assignment.
   */
  AbstractView& operator=(const AbstractView&) = delete;

  /**
   * @brief Attach controller to this view.
   */
  void SetController(gui::controllers::AbstractController* controller);
  gui::controllers::AbstractController* controller() const {
    return controller_;
  }

  void OnEnterPage();
  void OnExitPage();
  bool CanExitPage();

 protected:
  virtual void SetupUi() = 0;
  virtual void InstallSignalHandlers() = 0;

  /**
   * @brief Handle widget show event.
   */
  void showEvent(QShowEvent* event) override;

 private:
  void InitializeView();
  
  gui::controllers::AbstractController* controller_;
  bool is_initialized_;
};
}  // namespace views
}  // namespace gui
#endif