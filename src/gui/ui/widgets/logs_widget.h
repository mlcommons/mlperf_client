#ifndef LOGS_WIDGET_H
#define LOGS_WIDGET_H

#include <QTextCursor>
#include <QWidget>

class QTextEdit;

class LogsWidget : public QWidget {
  Q_OBJECT
 public:
  LogsWidget(QWidget *parent = nullptr);
  void AppendLogMessage(const QString &log);
  void InsertLogMessage(const QString &log, bool move_start, bool move_up,
                        bool move_down);
  void ClearLogs();

 private:
  void SetupUI();

  QTextEdit *logs_text_widget_;
  QTextCursor cursor_;
};

#endif  // LOGS_WIDGET_H
