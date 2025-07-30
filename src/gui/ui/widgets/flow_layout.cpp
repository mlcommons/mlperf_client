#include "flow_layout.h"

#include <QStyle>
#include <QWidget>
#include <QWidgetItem>

FlowLayout::FlowLayout(QWidget* parent, int margin, int h_spacing,
                       int v_spacing)
    : QLayout(parent), h_space_(h_spacing), v_space_(v_spacing) {
  setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
  QLayoutItem* item;
  while ((item = takeAt(0))) {
    delete item;
  }
}

void FlowLayout::addItem(QLayoutItem* item) { item_list_.push_back(item); }

int FlowLayout::horizontalSpacing() const {
  if (h_space_ >= 0) {
    return h_space_;
  }
  return SmartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const {
  if (v_space_ >= 0) {
    return v_space_;
  }
  return SmartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const { return static_cast<int>(item_list_.size()); }

QLayoutItem* FlowLayout::itemAt(int index) const {
  return index >= 0 && index < static_cast<int>(item_list_.size())
             ? item_list_[index]
             : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index) {
  if (index >= 0 && index < static_cast<int>(item_list_.size())) {
    auto it = item_list_.begin() + index;
    QLayoutItem* item = *it;
    item_list_.erase(it);
    return item;
  }
  return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const { return {}; }

bool FlowLayout::hasHeightForWidth() const { return true; }

int FlowLayout::heightForWidth(int width) const {
  int height = DoLayout(QRect(0, 0, width, 0), true);
  return height;
}

void FlowLayout::setGeometry(const QRect& rect) {
  QLayout::setGeometry(rect);
  DoLayout(rect, false);
}

QSize FlowLayout::sizeHint() const { return minimumSize(); }

QSize FlowLayout::minimumSize() const {
  QSize size;
  for (const QLayoutItem* item : item_list_) {
    size = size.expandedTo(item->minimumSize());
  }

  const QMargins margins = contentsMargins();
  size +=
      QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
  return size;
}

int FlowLayout::DoLayout(const QRect& rect, bool test_only) const {
  int left, top, right, bottom;
  getContentsMargins(&left, &top, &right, &bottom);
  QRect effective_rect = rect.adjusted(+left, +top, -right, -bottom);
  int x = effective_rect.x();
  int y = effective_rect.y();
  int line_height = 0;

  for (QLayoutItem* item : item_list_) {
    const QWidget* wid = item->widget();
    int space_x = horizontalSpacing();
    if (space_x == -1) {
      space_x = wid->style()->layoutSpacing(
          QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
    }
    int space_y = verticalSpacing();
    if (space_y == -1) {
      space_y = wid->style()->layoutSpacing(
          QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
    }

    int next_x = x + item->sizeHint().width() + space_x;
    if (next_x - space_x > effective_rect.right() && line_height > 0) {
      x = effective_rect.x();
      y = y + line_height + space_y;
      next_x = x + item->sizeHint().width() + space_x;
      line_height = 0;
    }

    if (!test_only) {
      item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
    }

    x = next_x;
    line_height = qMax(line_height, item->sizeHint().height());
  }
  return y + line_height - rect.y() + bottom;
}

int FlowLayout::SmartSpacing(QStyle::PixelMetric pm) const {
  QObject* parent = this->parent();
  if (!parent) {
    return -1;
  }
  if (parent->isWidgetType()) {
    QWidget* pw = static_cast<QWidget*>(parent);
    return pw->style()->pixelMetric(pm, nullptr, pw);
  }
  return -1;
}
