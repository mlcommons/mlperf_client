#include "elided_label.h"

#include <QFontMetrics>
#include <QPainter>
#include <QTextLayout>

ElidedLabel::ElidedLabel(QWidget* parent) : ElidedLabel(QString(), parent) {}

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent)
    : QWidget(parent), original_text_(text) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ElidedLabel::SetText(const QString& text) {
  if (original_text_ == text) return;

  original_text_ = text;
  update();
}

void ElidedLabel::SetElideMode(Qt::TextElideMode mode) {
  if (elide_mode_ == mode) return;

  elide_mode_ = mode;
  update();
}

void ElidedLabel::SetPreferredLineCount(int count) {
  preferred_line_count_ = count;
}

void ElidedLabel::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);

  const QFontMetrics fm(painter.fontMetrics());
  QTextLayout layout(original_text_, painter.font());

  QList<QTextLine> lines;
  QString elided_last_line;
  int total_text_height = 0;
  const int line_spacing = fm.lineSpacing();

  layout.beginLayout();
  QTextLine line = layout.createLine();
  while (line.isValid()) {
    line.setLineWidth(width());

    int next_line = total_text_height + line_spacing * 2;
    if (next_line <= height()) {
      lines.append(line);
      total_text_height += line_spacing;
    } else {
      QString remaining_text = original_text_.mid(line.textStart());
      elided_last_line = fm.elidedText(remaining_text, elide_mode_, width());
      total_text_height += line_spacing;
      break;
    }
    line = layout.createLine();
  }
  layout.endLayout();

  // Center vertically
  int y = (height() - total_text_height) / 2;

  // Draw all non-elided lines
  for (const QTextLine& line : lines) {
    line.draw(&painter, QPoint(0, y));
    y += line_spacing;
  }

  // Draw elided line
  if (!elided_last_line.isEmpty()) {
    painter.drawText(QPoint(0, y + fm.ascent()), elided_last_line);
  }
}

bool ElidedLabel::hasHeightForWidth() const {
  return preferred_line_count_ != -1;
}

int ElidedLabel::heightForWidth(int width) const {
  if (preferred_line_count_ == -1) return QWidget::heightForWidth(width);

  QFontMetrics fm(font());
  QTextLayout layout(original_text_, font());
  layout.beginLayout();

  int height = 0;
  for (int lines = 0; lines < preferred_line_count_; ++lines) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;

    line.setLineWidth(width);
    height += fm.lineSpacing();
  }

  layout.endLayout();

  return height;
}