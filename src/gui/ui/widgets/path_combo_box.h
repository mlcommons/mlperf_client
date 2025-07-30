#ifndef PATH_COMBO_BOX_H_
#define PATH_COMBO_BOX_H_

#include <QComboBox>

class PathComboBox : public QComboBox {
  Q_OBJECT

 public:
  explicit PathComboBox(QWidget* parent = nullptr);

  void SetPredefinedPaths(const QStringList& paths);
  void SetSelectedPath(const QString& path);
  QString GetSelectedPath() const;

 signals:
  void PathChanged(const QString& new_path);

 private slots:
  void OnCurrentTextChanged(const QString& text);

 private:
  QString previous_selected_path;
};

#endif  // PATH_COMBO_BOX_H_