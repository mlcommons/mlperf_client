#include "path_combo_box.h"

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

void PathComboBox::OnCurrentTextChanged(const QString& text) {
  if (text == "Browse...") {
    QString new_path = QFileDialog::getExistingDirectory(
        dynamic_cast<QWidget*>(parent()), "Select Data Directory");

    if (cil::utils::IsDirectoryWritable(new_path.toStdString())) {
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
