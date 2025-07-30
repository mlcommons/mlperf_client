#ifndef __ABSTRACT_CONTROLLER_H__
#define __ABSTRACT_CONTROLLER_H__

#include <QObject>

#include "../ui/abstract_view.h"
#include "core/types.h"

namespace gui {
namespace controllers {
class AbstractController : public QObject {
  Q_OBJECT
 public:
  explicit AbstractController(QObject* parent = nullptr) : QObject(parent) {}
  virtual ~AbstractController() = default;

  // Disable copy and assignment
  AbstractController(const AbstractController&) = delete;
  AbstractController& operator=(const AbstractController&) = delete;

  // Add view to the controller
  void SetView(gui::views::AbstractView* view);
  gui::views::AbstractView* view() const { return view_; }

 signals:
  void RequestPageChange(PageType page_type);

 protected:
  gui::views::AbstractView* view_;
};
}  // namespace controllers
}  // namespace gui

#endif  // __ABSTRACT_CONTROLLER_H__