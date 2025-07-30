#include "abstract_view.h"

#include <QDebug>

namespace gui {
namespace views {
AbstractView::AbstractView(QWidget* parent)
    : QWidget(parent), controller_(nullptr), is_initialized_(false) {
  // make sure it styles correctly
  setAttribute(Qt::WA_StyledBackground, true);
}

void AbstractView::SetController(
    gui::controllers::AbstractController* controller) {
  if (controller_ != controller) {
    controller_ = controller;
    if (!is_initialized_) {
      InitializeView();
    }
  }
}

void AbstractView::OnEnterPage() {}

void AbstractView::OnExitPage() {}

bool AbstractView::CanExitPage() { return true; }

void AbstractView::InitializeView() {
  SetupUi();
  InstallSignalHandlers();
  is_initialized_ = true;
}

void AbstractView::showEvent(QShowEvent* event) {
  if (!is_initialized_) {
    InitializeView();
  }
  QWidget::showEvent(event);
}

}  // namespace views
}  // namespace gui
