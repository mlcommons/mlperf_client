#ifndef RESULTS_HISTORY_MODEL_H_
#define RESULTS_HISTORY_MODEL_H_

#include <QAbstractListModel>
#include <QDateTime>
#include <QSortFilterProxyModel>

#include "core/types.h"

namespace gui {
namespace models {

// Model class for results history
class ResultsHistoryModel : public QAbstractListModel {
  Q_OBJECT

 public:
  explicit ResultsHistoryModel(QObject *parent = nullptr);
  ~ResultsHistoryModel() override = default;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  void SetEntries(QList<HistoryEntry> &entries);
  const HistoryEntry &GetEntry(int row) const;
  void RemoveEntry(int row);
  void RemoveAllEntries();

 private:
  QList<HistoryEntry> entries_;
};

// Proxy model class for filtering and sorting
class ResultsHistoryProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

 public:
  explicit ResultsHistoryProxyModel(QObject *parent = nullptr);

  void SetFilterDeviceTypes(const QStringList &device_types);
  void SetSortingMode(const QString &mode);

 protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;
  bool lessThan(const QModelIndex &left,
                const QModelIndex &right) const override;

 private:
  QStringList filter_device_types_;
  QString sorting_mode_;

  int DeviceTypeOrder(const QModelIndex &index,
                      const QString &priority = "CPU") const;
};

}  // namespace models
}  // namespace gui

#endif  // RESULTS_HISTORY_MODEL_H_
