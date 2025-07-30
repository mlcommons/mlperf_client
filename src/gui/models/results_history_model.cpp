#include "results_history_model.h"

#include <QSize>

#include "core/types.h"

namespace gui {
namespace models {
// Implementation of ResultsHistoryModel
ResultsHistoryModel::ResultsHistoryModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ResultsHistoryModel::rowCount(const QModelIndex &parent) const {
  return entries_.size();
}

QVariant ResultsHistoryModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
    return QVariant();

  const HistoryEntry &entry = entries_.at(index.row());

  switch (role) {
    case Qt::SizeHintRole:
      return QSize(0, 62);
    case Qt::UserRole:
      return entry.scenario_name_;
    case Qt::UserRole + 1:
      return entry.ep_name_;
    case Qt::UserRole + 2:
      return entry.device_type_;
    case Qt::UserRole + 3:
      return entry.date_time_;
    case Qt::UserRole + 4:
      return entry.success_;
    case Qt::UserRole + 5:
      if (entry.perf_results_map_.empty()) return 0.0;
      if (auto it = entry.perf_results_map_.find("Overall");
          it != entry.perf_results_map_.end())
        return it.value().time_to_first_token_;
      return 0.0;
    case Qt::UserRole + 6:
      if (entry.perf_results_map_.empty()) return 0.0;
      if (auto it = entry.perf_results_map_.find("Overall");
          it != entry.perf_results_map_.end())
        return it.value().token_generation_rate_;
      return 0.0;
    case Qt::UserRole + 7:
      return entry.error_message_;
    case Qt::UserRole + 8:
      return entry.ep_display_name_;
    case Qt::UserRole + 9:
      return entry.system_info_.os_name;
    case Qt::UserRole + 10:
      return entry.system_info_.cpu_name;
    case Qt::UserRole + 11:
      return entry.system_info_.ram;
    case Qt::UserRole + 12:
      return entry.system_info_.gpu_name;
    case Qt::UserRole + 13:
      return entry.system_info_.gpu_ram;
    case Qt::UserRole + 14:
      return QVariant::fromValue(entry.system_info_);
    default:
      return QVariant();
  }
}

void ResultsHistoryModel::SetEntries(QList<HistoryEntry> &entries) {
  if (!entries_.isEmpty()) RemoveAllEntries();

  if (entries.isEmpty()) return;

  beginInsertRows(QModelIndex(), 0, entries.size() - 1);
  this->entries_ = std::move(entries);
  endInsertRows();
}

const HistoryEntry &ResultsHistoryModel::GetEntry(int row) const {
  return this->entries_.at(row);
}

void ResultsHistoryModel::RemoveEntry(int row) {
  if (row < 0 || row >= entries_.size()) return;

  beginRemoveRows(QModelIndex(), row, row);
  entries_.removeAt(row);
  endRemoveRows();
}

void ResultsHistoryModel::RemoveAllEntries() {
  beginRemoveRows(QModelIndex(), 0, entries_.size() - 1);
  entries_.clear();
  endRemoveRows();
}

ResultsHistoryProxyModel::ResultsHistoryProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {}

void ResultsHistoryProxyModel::SetFilterDeviceTypes(
    const QStringList &device_types) {
  filter_device_types_ = device_types;
  invalidateRowsFilter();
}

void ResultsHistoryProxyModel::SetSortingMode(const QString &mode) {
  sorting_mode_ = mode;
  invalidate();
}

bool ResultsHistoryProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex &sourceParent) const {
  QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

  QString device_type = index.data(Qt::UserRole + 2).toString();
  return filter_device_types_.isEmpty() ||
         filter_device_types_.contains(device_type);
}

bool ResultsHistoryProxyModel::lessThan(const QModelIndex &left,
                                        const QModelIndex &right) const {
  if (sorting_mode_ == "EP name: A - Z") {
    return left.data(Qt::UserRole + 1).toString() <
           right.data(Qt::UserRole + 1).toString();
  } else if (sorting_mode_ == "EP name: Z - A") {
    return left.data(Qt::UserRole + 1).toString() >
           right.data(Qt::UserRole + 1).toString();
  } else if (sorting_mode_ == "CPUs first") {
    return DeviceTypeOrder(left) < DeviceTypeOrder(right);
  } else if (sorting_mode_ == "GPUs first") {
    return DeviceTypeOrder(left, "GPU") < DeviceTypeOrder(right, "GPU");
  } else if (sorting_mode_ == "NPUs first") {
    return DeviceTypeOrder(left, "NPU") < DeviceTypeOrder(right, "NPU");
  } else if (sorting_mode_ == "Newest first") {
    return left.data(Qt::UserRole + 3).toDateTime() >
           right.data(Qt::UserRole + 3).toDateTime();
  } else if (sorting_mode_ == "Oldest first") {
    return left.data(Qt::UserRole + 3).toDateTime() <
           right.data(Qt::UserRole + 3).toDateTime();
  }
  return QSortFilterProxyModel::lessThan(left, right);
}

int ResultsHistoryProxyModel::DeviceTypeOrder(const QModelIndex &index,
                                              const QString &priority) const {
  QString deviceType = index.data(Qt::UserRole + 2).toString();
  if (deviceType == priority) return 0;
  if (deviceType == "CPU") return 1;
  if (deviceType == "GPU") return 2;
  if (deviceType == "NPU") return 3;
  return 4;
}

}  // namespace models
}  // namespace gui