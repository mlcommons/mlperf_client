#ifndef FLOW_LAYOUT_H
#define FLOW_LAYOUT_H

#include <QLayout>
#include <QRect>
#include <QStyle>
#include <vector>

/**
 * @class FlowLayout
 * @brief Layout that flows widgets into rows with automatic wrapping.
 */
class FlowLayout : public QLayout {
 public:
  explicit FlowLayout(QWidget* parent = nullptr, int margin = -1,
                      int h_spacing = -1, int v_spacing = -1);
  ~FlowLayout();

  void addItem(QLayoutItem* item) override;

  /**
   * @brief Return configured spacing or calculate smart spacing from system style.
   */  
  int horizontalSpacing() const;
  int verticalSpacing() const;

  Qt::Orientations expandingDirections() const override;

  /**
   * @brief Indicates if this layout's height depends on width.
   */
  bool hasHeightForWidth() const override;

  /**
   * @brief Uses DoLayout in test mode to calculate required height.
   * @return Required height for the given width.
   */
  int heightForWidth(int width) const override;

  int count() const override;
  QLayoutItem* itemAt(int index) const override;
  QSize minimumSize() const override;

  /**
   * @brief Calls DoLayout to actually position all items.
   * @param rect Rectangle defining the layout area.
   */
  void setGeometry(const QRect& rect) override;
  
  QSize sizeHint() const override;
  QLayoutItem* takeAt(int index) override;

 private:
  /**
   * @brief Arranges items in rows, wrapping when needed.
   * @param rect Rectangle defining the layout area.
   * @param test_only If true, only calculates size without positioning items.
   * @return Required height for the layout.
   */
  int DoLayout(const QRect& rect, bool test_only) const;

  /**
   * @brief Fallback to get system-appropriate spacing when -1 is specified.
   * @param pm Pixel metric type to query from system style.
   * @return Spacing value from system style.
   */
  int SmartSpacing(QStyle::PixelMetric pm) const;

  std::vector<QLayoutItem*> item_list_;
  int h_space_;
  int v_space_;
};

#endif  // FLOW_LAYOUT_H