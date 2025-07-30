#include "logs_widget.h"

#include <QLabel>
#include <QScrollBar>
#include <QTextEdit>
#include <QVBoxLayout>

#ifdef Q_OS_IOS
#include <QScroller>
#endif

namespace {
QString WrapLongLines(const QString &input, int max_chars_per_line = 120) {
  if (input.size() <= max_chars_per_line) return input;

  const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
  QStringList wrapped_lines;
  for (const QString &line : lines) {
    int start = 0;
    int len = line.length();

    while (start < len) {
      int remaining = len - start;
      if (remaining <= max_chars_per_line) {
        wrapped_lines.append(line.mid(start));
        break;
      }

      int end = start + max_chars_per_line;
      int last_space = line.lastIndexOf(' ', end - 1);

      if (last_space <= start) {  // hard wrap
        wrapped_lines.append(line.mid(start, max_chars_per_line));
        start += max_chars_per_line;
      } else {  // soft wrap at space
        wrapped_lines.append(line.mid(start, last_space - start));
        start = last_space + 1;
      }
    }

    if (len == 0) wrapped_lines.append(QString());
  }

  return wrapped_lines.join(u'\n');
}
}  // namespace

LogsWidget::LogsWidget(QWidget *parent)
    : QWidget(parent), logs_text_widget_(nullptr) {
  SetupUI();
}

void LogsWidget::AppendLogMessage(const QString &log) {
  logs_text_widget_->append(WrapLongLines(log));
  cursor_.movePosition(QTextCursor::End);
  logs_text_widget_->verticalScrollBar()->setValue(
      logs_text_widget_->verticalScrollBar()->maximum());
}

void LogsWidget::InsertLogMessage(const QString &log, bool move_start,
                                  bool move_up, bool move_down) {
  if (move_start) cursor_.movePosition(QTextCursor::StartOfLine);

  if (move_up) {
    cursor_.movePosition(QTextCursor::StartOfLine);
    cursor_.movePosition(QTextCursor::PreviousCharacter);
  }

  if (move_down) {
    if (cursor_.atEnd()) {
      cursor_.insertText("\n");
    } else {
      cursor_.movePosition(QTextCursor::EndOfLine);
      cursor_.movePosition(QTextCursor::NextCharacter);
    }
  }

  if (!log.isEmpty()) {
    cursor_.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    cursor_.insertText(log);
  }
  logs_text_widget_->verticalScrollBar()->setValue(
      logs_text_widget_->verticalScrollBar()->maximum());
}

void LogsWidget::ClearLogs() { logs_text_widget_->clear(); }

void LogsWidget::SetupUI() {
  setAttribute(Qt::WA_StyledBackground, true);
  setProperty("class", "panel_widget");

  QLabel *logs_widget_title_label = new QLabel("Logs Information", this);
  logs_widget_title_label->setProperty("class", "title_label");
  logs_text_widget_ = new QTextEdit(this);
  logs_text_widget_->setReadOnly(true);
  logs_text_widget_->setLineWrapMode(QTextEdit::NoWrap);
  cursor_ = QTextCursor(logs_text_widget_->document());
  QVBoxLayout *main_layout = new QVBoxLayout();

  main_layout->setContentsMargins(50, 50, 50, 50);
  main_layout->setSpacing(30);
  main_layout->addWidget(logs_widget_title_label);
  main_layout->addWidget(logs_text_widget_);

  setLayout(main_layout);

#ifdef Q_OS_IOS
  logs_text_widget_->setTextInteractionFlags(Qt::NoTextInteraction);
  QScroller::grabGesture(logs_text_widget_, QScroller::TouchGesture);
#endif
}
