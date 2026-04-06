#ifndef POPUP_WIDGET_H_
#define POPUP_WIDGET_H_

#include <QDialog>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QProgressBar;
class QCheckBox;

class PopupWidget : public QDialog {
  Q_OBJECT

 public:
  /**
   * @brief Construct popup widget.
   * @param parent Parent widget, defaults to nullptr.
   */
  explicit PopupWidget(QWidget* parent = nullptr);

  /**
   * @brief Set the message text to display in the popup.
   */
  void SetMessage(const QString& message);

  /**
   * @brief Static convenience method to show a message popup.
   */
  static void ShowMessage(const QString& message, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;

  QVBoxLayout* main_layout_;

 private:
  QLabel* message_label_;
  QPushButton* close_button_;
};

class QuestionPopupWidget : public PopupWidget {
  Q_OBJECT
 public:
  /**
   * @brief Construct question popup widget.
   * @param parent Parent widget, defaults to nullptr.
   * @param show_do_not_ask_again If true, shows "Do not ask again" checkbox.
   */
  explicit QuestionPopupWidget(QWidget* parent = nullptr,
                               bool show_do_not_ask_again = false);

  /**
   * @brief Returns whether the "Do not ask again" checkbox is checked.
   * @return True if the checkbox exists and is checked, false otherwise.
   */
  bool DoNotAskAgainChecked() const;

 private:
  QCheckBox* do_not_ask_again_checkbox_ = nullptr;
};

class ProgressPopupWidget : public PopupWidget {
  Q_OBJECT
 public:
  /**
   * @brief Construct progress popup widget.
   * @param parent Parent widget, defaults to nullptr.
   */
  explicit ProgressPopupWidget(QWidget* parent = nullptr);

  /**
   * @brief Set progress value and update the progress bar.
   * The provided value is clamped to the valid range [0, 100].
   * @param percent Progress value in percent.
   */
  void SetProgressPercent(int percent);

  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  QProgressBar* progress_bar_;
  QLabel* progress_percent_label_;
};

#endif  // POPUP_WIDGET_H_