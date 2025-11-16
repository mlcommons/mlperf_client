#ifndef PATH_COMBO_BOX_H_
#define PATH_COMBO_BOX_H_

#include <QComboBox>

/**
 * @class PathComboBox
 * @brief Combo box widget for file path selection.
 */
class PathComboBox : public QComboBox {
  Q_OBJECT

 public:
  explicit PathComboBox(QWidget* parent = nullptr);

  /** 
   * @brief Sets the list of predefined paths.
   */
  void SetPredefinedPaths(const QStringList& paths);

  /**
   * @brief Sets the currently selected path in the combo box
   */
  void SetSelectedPath(const QString& path);

  /**
   * @brief Gets the currently selected file path
   */
  QString GetSelectedPath() const;

 signals:
  void PathChanged(const QString& new_path);

 private slots:
  /** 
   * @brief Handles text changes in the combo box.
   */
  void OnCurrentTextChanged(const QString& text);

 private:
  QString previous_selected_path; 
};

#endif  // PATH_COMBO_BOX_H_