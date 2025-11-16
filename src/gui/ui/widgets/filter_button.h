#ifndef FILTER_BUTTON_H_
#define FILTER_BUTTON_H_

#include <QPushButton>
#include <QStringList>

class QMenu;
class QActionGroup;

/**
 * @class FilterButton
 * @brief Button widget with dropdown menu for filter selection.
 */
class FilterButton : public QPushButton {
  Q_OBJECT

 public:
  explicit FilterButton(QWidget *parent = nullptr);

  void SetActions(const QStringList &actions);

  void SetExclusive(bool exclusive);

  void SelectAction(const QString &action);

  QStringList CurrentSelection() const;

 signals:
  void SelectionChanged(const QStringList &selection);

 private slots:
  /**
   * @brief Handles menu action triggers.
   */
  void OnActionTriggered(QAction *action);

 private:
  QMenu *menu_;
  QActionGroup *action_group_;
};

#endif  // FILTER_BUTTON_H_