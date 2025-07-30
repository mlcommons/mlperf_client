#include "controllers/abstract_controller.h"

#include "ui/abstract_view.h"

namespace gui {
namespace controllers {
void AbstractController::SetView(gui::views::AbstractView* view) {
  if (view_ != view) {
    view_ = view;
    view_->SetController(this);
  }
}
}  // namespace controllers
}  // namespace gui
