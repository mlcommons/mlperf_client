#ifndef CHECKABLE_COMBO_BOX_H_
#define CHECKABLE_COMBO_BOX_H_

#include <QComboBox>

class QPaintEvent;
class QStandardItemModel;
class QModelIndex;

/**
 * @class CheckableComboBox
 * @brief Combo box with checkable items for multi-selection filtering.
 */
class CheckableComboBox : public QComboBox {
  Q_OBJECT

 public:
  explicit CheckableComboBox(const QList<QPair<QString, bool>> &options,
                             QWidget *parent = nullptr);
  QList<QPair<QString, bool>> GetOptions() const;

 protected:
  void paintEvent(QPaintEvent *);

 signals:
  void FilterChanged();

 private slots:
 private slots:
  /**
   * @brief Handle item selection/deselection.
   * @param index Model index of the pressed item.
   */
  void OnIndexPressed(const QModelIndex &index);

  /**
   * @brief Handle model data changes.
   * @param top_left Top-left model index of changed area.
   * @param bottom_right Bottom-right model index of changed area.
   * @param roles List of data roles that changed.
   */
  void OnDataChanged(const QModelIndex &top_left,
                     const QModelIndex &bottom_right, const QList<int> &roles);

 private:
  void UpdateText();

  QList<QPair<QString, bool>> options_;
  QStandardItemModel *model_;
  QString header_text_;
};

#endif  // CHECKABLE_COMBO_BOX_H_