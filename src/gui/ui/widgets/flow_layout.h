#ifndef FLOW_LAYOUT_H
#define FLOW_LAYOUT_H

#include <QLayout>
#include <QRect>
#include <QStyle>
#include <vector>

class FlowLayout : public QLayout {
 public:
  explicit FlowLayout(QWidget* parent = nullptr, int margin = -1,
                      int h_spacing = -1, int v_spacing = -1);
  ~FlowLayout();

  void addItem(QLayoutItem* item) override;
  int horizontalSpacing() const;
  int verticalSpacing() const;
  Qt::Orientations expandingDirections() const override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;
  int count() const override;

  QLayoutItem* itemAt(int index) const override;
  QSize minimumSize() const override;
  void setGeometry(const QRect& rect) override;
  QSize sizeHint() const override;
  QLayoutItem* takeAt(int index) override;

 private:
  int DoLayout(const QRect& rect, bool test_only) const;
  int SmartSpacing(QStyle::PixelMetric pm) const;

  std::vector<QLayoutItem*> item_list_;
  int h_space_;
  int v_space_;
};
#endif  // FLOW_LAYOUT_H