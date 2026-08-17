#include "path_combo_box.h"

#include <QDir>
#include <QFileDialog>
#include <QListView>

#include "utils.h"

PathComboBox::PathComboBox(QWidget* parent) : QComboBox(parent) {
  // Forces QComboBox to use a Qt list view instead of the native view on iOS
  setView(new QListView());

  connect(this, &QComboBox::currentTextChanged, this,
          &PathComboBox::OnCurrentTextChanged);
}

void PathComboBox::SetPredefinedPaths(const QStringList& paths) {
  clear();
  addItems(paths);
#ifndef Q_OS_IOS
  addItem("Browse...");
#endif
}

void PathComboBox::SetSelectedPath(const QString& path) {
  if (path.isEmpty()) return;
  int index = findText(path);
  if (index == -1) {
    insertItem(0, path);
    index = 0;
  }
  previous_selected_path = path;
  blockSignals(true);
  setCurrentIndex(index);
  blockSignals(false);
}

QString PathComboBox::GetSelectedPath() const { return currentText(); }

void PathComboBox::SetRequireWritable(bool require_writable) {
  require_writable_ = require_writable;
}

void PathComboBox::SetBrowseDialogTitle(const QString& title) {
  browse_dialog_title_ = title;
}

void PathComboBox::OnCurrentTextChanged(const QString& text) {
  if (text == "Browse...") {
    QString new_path = QFileDialog::getExistingDirectory(
        dynamic_cast<QWidget*>(parent()), browse_dialog_title_);

    const bool acceptable =
        require_writable_
            ? cil::utils::IsDirectoryWritable(new_path.toStdString())
            : !new_path.isEmpty() && QDir(new_path).exists();
    if (acceptable) {
      int existing_index = findText(new_path);
      blockSignals(true);
      if (existing_index == -1) {
        insertItem(0, new_path);
        setCurrentIndex(0);
      } else {
        setCurrentIndex(existing_index);
      }
      blockSignals(false);
      previous_selected_path = new_path;
      emit PathChanged(new_path);
    } else {
      blockSignals(true);
      setCurrentText(previous_selected_path);
      blockSignals(false);
    }
  } else {
    previous_selected_path = text;
    emit PathChanged(text);
  }
}
