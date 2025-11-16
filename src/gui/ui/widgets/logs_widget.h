#ifndef LOGS_WIDGET_H
#define LOGS_WIDGET_H

#include <QTextCursor>
#include <QWidget>

class QTextEdit;

/**
 * @class LogsWidget
 * @brief Widget for real-time log message display.
 */
class LogsWidget : public QWidget {
  Q_OBJECT

 public:
  LogsWidget(QWidget *parent = nullptr);

  /**
   * @brief Append a log message to the end of the log display.
   * @param log Log message text.
   */
  void AppendLogMessage(const QString &log);

  /**
   * @brief Insert a log message with cursor positioning options.
   * @param move_start Boolean indicating whether to move cursor to start.
   * @param move_up Boolean indicating whether to move cursor up.
   * @param move_down Boolean indicating whether to move cursor down.
   */
  void InsertLogMessage(const QString &log, bool move_start, bool move_up,
                        bool move_down);
  void ClearLogs();

 private:
  void SetupUI();

  QTextEdit *logs_text_widget_;
  QTextCursor cursor_;
};

#endif  // LOGS_WIDGET_H