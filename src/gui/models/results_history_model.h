#ifndef RESULTS_HISTORY_MODEL_H_
#define RESULTS_HISTORY_MODEL_H_

#include <QAbstractListModel>
#include <QDateTime>
#include <QSortFilterProxyModel>

#include "core/types.h"

namespace gui {
namespace models {

/**
 * @class ResultsHistoryModel
 * @brief Qt model for benchmark results history.
 *
 * Stores and provides access to benchmark history entries for Qt views.
 */
class ResultsHistoryModel : public QAbstractListModel {
  Q_OBJECT

 public:
  explicit ResultsHistoryModel(QObject *parent = nullptr);
  ~ResultsHistoryModel() override = default;

  /**
   * @brief Number of entries in the model.
   * @param parent Parent model index, defaults to invalid index.
   */
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;

  /**
   * @brief Data for a given index and role.
   * @return QVariant containing the requested data.
   */
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  /**
   * @brief Set the complete list of history entries.
   */
  void SetEntries(QList<HistoryEntry> &entries);

  /**
   * @brief Get a specific history entry by row.
   * @param row Row index of the entry to retrieve.
   * @return Reference to the history entry at the specified row.
   */
  const HistoryEntry &GetEntry(int row) const;

  /**
   * @brief Remove a specific entry by row index.
   */
  void RemoveEntry(int row);

  /**
   * @brief Remove all entries from the model.
   */
  void RemoveAllEntries();

 private:
  QList<HistoryEntry> entries_;
};

/**
 * @class ResultsHistoryProxyModel
 * @brief Qt proxy model for filtering and sorting benchmark history.
 *
 * Adds filtering by device type and custom sorting to the history model.
 */
class ResultsHistoryProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

 public:
  /**
   * @brief Construct proxy model for benchmark history.
   * @param parent Parent QObject, defaults to nullptr.
   */
  explicit ResultsHistoryProxyModel(QObject *parent = nullptr);

  /**
   * @brief Set device types to filter by.
   * @param device_types List of device types to include in filtering.
   */
  void SetFilterDeviceTypes(const QStringList &device_types);

  /**
   * @brief Set the sorting mode for the model.
   */
  void SetSortingMode(const QString &mode);

 protected:
  /**
   * @brief Filter rows based on device type.
   * @return True if the row should be included in the filtered results.
   */
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;

  /**
   * @brief Compare two entries for sorting.
   * @return True if left item should be sorted before right item.
   */
  bool lessThan(const QModelIndex &left,
                const QModelIndex &right) const override;

 private:
  QStringList filter_device_types_;
  QString sorting_mode_;

  /**
   * @brief Determine sort order for device types.
   * @param index Model index to get device type from.
   * @param priority Priority device type for ordering, defaults to "CPU".
   * @return Integer representing the sort order for the device type.
   */
  int DeviceTypeOrder(const QModelIndex &index,
                      const QString &priority = "CPU") const;
};

}  // namespace models
}  // namespace gui

#endif  // RESULTS_HISTORY_MODEL_H_
