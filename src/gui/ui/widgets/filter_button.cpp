#include "filter_button.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>

FilterButton::FilterButton(QWidget *parent)
    : QPushButton(parent),
      menu_(new QMenu(this)),
      action_group_(new QActionGroup(this)) {
  setMenu(menu_);
  action_group_->setExclusive(false);
  connect(action_group_, &QActionGroup::triggered, this,
          &FilterButton::OnActionTriggered);
}

void FilterButton::SetActions(const QStringList &actions) {
  menu_->clear();

  for (QAction *action : action_group_->actions()) {
    action_group_->removeAction(action);
    delete action;
  }

  for (const QString &action_text : actions) {
    QAction *action = new QAction(action_text, this);
    action->setCheckable(true);
    menu_->addAction(action);
    action_group_->addAction(action);
  }
}

void FilterButton::SetExclusive(bool exclusive) {
  action_group_->setExclusive(exclusive);
}

void FilterButton::SelectAction(const QString &action) {
  auto actions = action_group_->actions();
  for (QAction *act : action_group_->actions())
    if (act->text() == action) {
      act->setChecked(true);
      emit SelectionChanged({action});
      break;
    }
}

QStringList FilterButton::CurrentSelection() const {
  QStringList selected_actions;
  for (QAction *act : action_group_->actions())
    if (act->isChecked()) selected_actions << act->text();
  return selected_actions;
}

void FilterButton::OnActionTriggered(QAction *action) {
  emit SelectionChanged(CurrentSelection());
}
