#ifndef EP_PROGRESS_WIDGET_H
#define EP_PROGRESS_WIDGET_H

#include <QWidget>

class QFrame;
class QLabel;

/**
 * @class EPProgressWidget
 * @brief Progress tracking widget for execution provider benchmarks.
 */
class EPProgressWidget : public QWidget {
  Q_OBJECT

 public:
  EPProgressWidget(const QString &name, const QString &description,
                   const QString &icon_path, const QString &long_name,
                   const QString &model_name, QWidget *parent = nullptr);

  /**
   * @brief Updates the progress display with current step information
   */
  void SetProgress(int total_steps, int current_step);

  /**
   * @brief Gets the file path of the execution provider icon
   */
  QString GetIconPath() const;

  /**
   * @brief Starts the progress tracking and updates visual state
   */
  void Start();

  /**
   * @brief Marks the benchmark as completed and updates visual state
   */
  void SetCompleted(bool success);

  /**
   * @brief Gets the short name of the execution provider
   * @return The execution provider name as QString
   */
  QString GetName() const;

  /**
   * @brief Gets the long name of the execution provider
   * @return The execution provider long name as QString
   */
  QString GetLongName() const;

  /**
   * @brief Sets the widget transparency for visual effects
   */
  void SetTransparent(bool transparent);

 private:
  void SetupUI();

  QFrame *content_frame_;
  QLabel *icon_label_;
  QLabel *model_label_;
  QLabel *ep_name_label_;
  QLabel *progress_label_;
  QString ep_name_;
  QString ep_long_name_;
  QString model_name_;
  QString ep_description_;
  QString icon_path_;
};

#endif  // EP_PROGRESS_WIDGET_H