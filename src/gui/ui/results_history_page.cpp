#include "results_history_page.h"

#include <QDateTime>

#ifdef Q_OS_IOS
#include <QScroller>
#endif

#include "core/types.h"
#include "widgets/popup_widget.h"
#include "widgets/results_history_entry_widget.h"

namespace {
static constexpr int HISTORY_VIEW_SPACING = 20;
}

namespace gui {
namespace views {

ResultsHistoryPage::ResultsHistoryPage(QWidget *parent)
    : AbstractView(parent) {}

void ResultsHistoryPage::SetModel(QAbstractItemModel *model) {
  ui_.history_list_view_->setModel(model);

  connect(ui_.history_list_view_->model(), &QAbstractItemModel::rowsInserted,
          this, &ResultsHistoryPage::OnRowsInserted);
  connect(ui_.history_list_view_->selectionModel(),
          &QItemSelectionModel::selectionChanged, this,
          &ResultsHistoryPage::OnRowSelectionChanged);
}

QItemSelectionModel *ResultsHistoryPage::GetSelectionModel() const {
  return ui_.history_list_view_->selectionModel();
}

void ResultsHistoryPage::SetupUi() {
  ui_.setupUi(this);

  ui_.history_results_title_->setProperty("class", "title");

  ui_.sort_by_button_->setProperty("class", "filter_button");
  ui_.device_type_button_->setProperty("class", "filter_button");

  ui_.select_all_button_->setProperty("class", "secondary_button");
  ui_.select_all_button_->setProperty("tiny", "true");
  ui_.deselect_all_button_->setProperty("class", "secondary_button");
  ui_.deselect_all_button_->setProperty("tiny", "true");

  ui_.delete_button_->setProperty("class", "secondary_button_with_icon");
  ui_.delete_button_->setProperty("has_border", true);
  ui_.open_report_button_->setProperty("class", "primary_button");

  ui_.history_results_container_->setProperty("class",
                                              "transparent_panel_widget");
  ui_.history_list_view_->setProperty("class", "history_results_list_view");

  ui_.sort_by_button_->SetActions({"Newest first", "Oldest first", "CPUs first",
                                   "GPUs first", "NPUs first", "EP name: A - Z",
                                   "EP name: Z - A"});
  ui_.sort_by_button_->SetExclusive(true);
  ui_.device_type_button_->SetActions(
      {"CPU", "GPU", "NPU", "NPU-CPU", "NPU-GPU"});

  ui_.history_list_view_->setVerticalScrollMode(
      QAbstractItemView::ScrollPerPixel);
  ui_.history_list_view_->setResizeMode(QListView::Adjust);
#ifdef Q_OS_IOS
  ui_.history_list_view_->setSelectionMode(QAbstractItemView::NoSelection);
#else
  ui_.history_list_view_->setSelectionMode(QAbstractItemView::MultiSelection);
#endif
  ui_.history_list_view_->setSpacing(HISTORY_VIEW_SPACING);

#ifdef Q_OS_IOS
  QScroller::grabGesture(ui_.history_list_view_);
#endif
}

void ResultsHistoryPage::InstallSignalHandlers() {
  connect(
      ui_.sort_by_button_, &FilterButton::SelectionChanged, this,
      [this](const QStringList &selection) {
        emit SortRequested(selection.empty() ? QString() : selection.front());
      });
  connect(ui_.device_type_button_, &FilterButton::SelectionChanged, this,
          &ResultsHistoryPage::DeviceTypeFilterRequested);
  connect(ui_.open_report_button_, &QPushButton::clicked, this,
          [this]() { emit OpenReportRequested(-1); });
  connect(ui_.delete_button_, &QPushButton::clicked, this,
          &ResultsHistoryPage::OnDeleteButtonClicked);
  connect(ui_.select_all_button_, &QPushButton::clicked, this,
          [this]() { emit SelectEntriesRequested(-1, true); });
  connect(ui_.deselect_all_button_, &QPushButton::clicked, this,
          [this]() { emit SelectEntriesRequested(-1, false); });
}

void ResultsHistoryPage::SetSortingMode(const QString &mode) {
  ui_.sort_by_button_->SelectAction(mode);
}

void ResultsHistoryPage::OnRowsInserted(const QModelIndex &parent, int first,
                                        int last) {
  for (int row = first; row <= last; ++row) {
    QModelIndex index = ui_.history_list_view_->model()->index(row, 0);
    if (!index.data(Qt::UserRole + 14).canConvert<SystemInfoDetails>())
      continue;
    SystemInfoDetails system_details =
        index.data(Qt::UserRole + 14).value<SystemInfoDetails>();
    auto widget = new ResultsHistoryEntryWidget(
        // ep name + device type + (scenario name)
        index.data(Qt::UserRole + 8).toString() + " (" +
            index.data(Qt::UserRole).toString() + ")",
        index.data(Qt::UserRole + 3).toDateTime(),  // date and time
        index.data(Qt::UserRole + 4).toBool(),      // success
        index.data(Qt::UserRole + 5).toDouble(),    // TTFT
        index.data(Qt::UserRole + 6).toDouble(),    // TPS
        index.data(Qt::UserRole + 7).toString(), system_details,
        this);  // error message

    ui_.history_list_view_->setIndexWidget(index, widget);
    if (ui_.history_list_view_->gridSize().height() < widget->height()) {
      ui_.history_list_view_->setGridSize(
          QSize(ui_.history_list_view_->viewport()->width(),
                widget->height() + HISTORY_VIEW_SPACING / 2));
    }
    connect(widget, &ResultsHistoryEntryWidget::OpenButtonClicked, this,
            &ResultsHistoryPage::OnRowOpenButtonClicked);
    connect(widget, &ResultsHistoryEntryWidget::SelectionBoxChecked, this,
            &ResultsHistoryPage::OnRowSelectionBoxChecked);
  }
}

void ResultsHistoryPage::OnRowSelectionChanged(
    const QItemSelection &selected, const QItemSelection &deselected) {
  auto updateEntriesSelection = [this](const QItemSelection &selection,
                                       bool selected) {
    for (const QModelIndex &index : selection.indexes())
      if (ResultsHistoryEntryWidget *entry_widget =
              qobject_cast<ResultsHistoryEntryWidget *>(
                  ui_.history_list_view_->indexWidget(index)))
        entry_widget->SetSelected(selected);
  };
  updateEntriesSelection(deselected, false);
  updateEntriesSelection(selected, true);

  UpdateButtons();
}

void ResultsHistoryPage::OnRowOpenButtonClicked() {
  ResultsHistoryEntryWidget *row_widget =
      dynamic_cast<ResultsHistoryEntryWidget *>(sender());

  int widget_row = -1;
  for (int row = 0; row < ui_.history_list_view_->model()->rowCount(); ++row) {
    QModelIndex index = ui_.history_list_view_->model()->index(row, 0);
    if (ui_.history_list_view_->indexWidget(index) == row_widget) {
      widget_row = row;
      break;
    }
  }

  if (widget_row != -1) emit OpenReportRequested(widget_row);
}

void ResultsHistoryPage::OnRowSelectionBoxChecked(bool checked) {
  ResultsHistoryEntryWidget *row_widget =
      dynamic_cast<ResultsHistoryEntryWidget *>(sender());

  for (int row = 0; row < ui_.history_list_view_->model()->rowCount(); ++row) {
    QModelIndex index = ui_.history_list_view_->model()->index(row, 0);
    if (ui_.history_list_view_->indexWidget(index) == row_widget) {
      emit SelectEntriesRequested(row, checked);
      break;
    }
  }
}

void ResultsHistoryPage::OnDeleteButtonClicked() {
  PopupWidget *popup = new PopupWidget(this, true);
  popup->setFixedSize(400, 250);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setModal(true);
  popup->SetMessage("Delete selected entries?\nThis action cannot be undone.");

  if (popup->exec() == QDialog::Accepted) emit DeleteHistoryRequested();
}

void ResultsHistoryPage::UpdateButtons() {
  bool passed_entry_selected = false;

  auto indexes = GetSelectionModel()->selectedIndexes();
  for (const QModelIndex &index : indexes)
    if (ResultsHistoryEntryWidget *entry_widget =
            qobject_cast<ResultsHistoryEntryWidget *>(
                ui_.history_list_view_->indexWidget(index)))
      if (entry_widget->IsPassed()) {
        passed_entry_selected = true;
        break;
      }
  ui_.open_report_button_->setEnabled(passed_entry_selected);
  ui_.delete_button_->setEnabled(indexes.size() > 0);
}

}  // namespace views
}  // namespace gui
