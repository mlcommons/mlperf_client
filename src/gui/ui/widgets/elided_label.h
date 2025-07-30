#ifndef ELIDED_LABEL_H_
#define ELIDED_LABEL_H_

#include <QWidget>

class ElidedLabel : public QWidget {
  Q_OBJECT
 public:
  explicit ElidedLabel(const QString& text, QWidget* parent = nullptr);
  explicit ElidedLabel(QWidget* parent = nullptr);

  void SetText(const QString& text);

  void SetElideMode(Qt::TextElideMode mode);
  void SetPreferredLineCount(int count);

 protected:
  void paintEvent(QPaintEvent* event) override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;

 private:
  QString original_text_;
  Qt::TextElideMode elide_mode_ = Qt::ElideRight;
  int preferred_line_count_ = -1;
};

#endif  // ELIDED_LABEL_H_
