#include "result_table_widget.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>

#include "../CIL/execution_provider.h"
#include "core/gui_utils.h"

namespace {
static constexpr int COLUMN_BORDER_RADIUS = 10;
static constexpr int SPACE_BETWEEN_SECTIONS = 25;
static constexpr int SECTION_TOP_MARGIN = 3;
static constexpr QColor SECTION_BORDER_COLOR(255, 255, 255, 100);
static constexpr int SECTION_BORDER_WIDTH = 3;
static constexpr QColor COLUMN_GRADIENT_COLOR_0(20, 88, 141, 153);
static constexpr QColor COLUMN_GRADIENT_COLOR_1(10, 44, 69, 153);
static constexpr QColor ROW_COLOR(10, 46, 73, 153);
}  // namespace

namespace custom_widgets {

CellWidget::CellWidget(const QString& text, int index, bool bold,
                       QWidget* parent)
    : QWidget(parent) {
  auto* min_layout = new QVBoxLayout(this);
  label_ = new QLabel(this);
  label_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  label_->setTextFormat(Qt::RichText);
  label_->setText(text);

  if (index == 0) {
    label_->setObjectName("first_cell_label");
    min_layout->addWidget(label_, 0, Qt::AlignCenter | Qt::AlignLeft);
    min_layout->setContentsMargins(20, 5, 5, 5);
  } else {
    label_->setObjectName("other_cell_label");
    min_layout->addWidget(label_, 0, Qt::AlignCenter);
    min_layout->setContentsMargins(0, 0, 0, 5);
  }
  if (bold)
    label_->setStyleSheet("font-weight: 700;");
  else
    label_->setStyleSheet("font-weight: 400;");
}

void CellWidget::SetText(const QString& text) { label_->setText(text); }

HeaderCell::HeaderCell(const HeaderInfo& header, QWidget* parent)
    : QWidget(parent) {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(5, 10, 5, 0);

  info_icon_label_ = new QLabel(this);
  info_icon_label_->setPixmap(
      QPixmap(":/icons/resources/icons/action_info-16.png").scaled(14, 14));

  QString verification_text;
  if (header.is_experimental)
    verification_text = "experimental";
  else
    verification_text = header.tested_by_ml_commons ? "tested by MLCommons"
                                                    : "not tested by MLCommons";
  tested_by_ml_commons_label_ = new QLabel(verification_text, this);

  auto* tested_by_layout = new QHBoxLayout();
  tested_by_layout->setContentsMargins(10, 10, 10, 10);
  tested_by_layout->addWidget(info_icon_label_);
  tested_by_layout->addWidget(tested_by_ml_commons_label_);

  QWidget* tested_by_widget = new QWidget(this);
  tested_by_widget->setLayout(tested_by_layout);
  tested_by_widget->setObjectName("testedByMLCommonsLabel");
  if (!header.tested_by_ml_commons || header.is_experimental)
    tested_by_widget->setProperty("tested", "false");

  date_icon_label_ = new QLabel(this);
  date_icon_label_->setPixmap(
      QPixmap(":/icons/resources/icons/calendar_icon.png").scaled(14, 14));

  date_label_ = new QLabel(header.datetime.toString("MMM dd, yyyy"), this);
  date_label_->setProperty("class", "small_strong_label");

  time_icon_label_ = new QLabel(this);
  time_icon_label_->setPixmap(
      QPixmap(":/icons/resources/icons/time_icon.png").scaled(14, 14));

  time_label_ = new QLabel(header.datetime.toString("h:mm AP"), this);
  time_label_->setProperty("class", "small_strong_label");

  auto* date_time_layout = new QHBoxLayout();
  date_time_layout->setSpacing(3);
  date_time_layout->addStretch();
  date_time_layout->addWidget(date_icon_label_);
  date_time_layout->addWidget(date_label_);
  date_time_layout->addSpacing(10);
  date_time_layout->addWidget(time_icon_label_);
  date_time_layout->addWidget(time_label_);
  date_time_layout->addStretch();

  model_label_ = new QLabel("Model " + header.model_name, this);
  model_label_->setProperty("class", "small_strong_label");
  QString ep_name_display =
      QString::fromStdString(cil::EPNameToLongName(header.ep.toStdString()));

  ep_label_ = new QLabel(ep_name_display, this);
  ep_label_->setProperty("class", "large_strong_label");

  device_label_ = new QLabel(header.device, this);
  device_label_->setProperty("class", "large_strong_label");
  device_icon_label_ = new QLabel(this);
  device_icon_label_->setPixmap(QPixmap(header.icon).scaled(18, 18));
  auto* icon_label_layout = new QHBoxLayout();
  icon_label_layout->addStretch();
  icon_label_layout->addWidget(ep_label_);
  icon_label_layout->addWidget(device_label_);
  icon_label_layout->addWidget(device_icon_label_);
  icon_label_layout->addStretch();

  main_layout->addWidget(tested_by_widget, 0, Qt::AlignCenter);
  main_layout->addLayout(date_time_layout);
  main_layout->addWidget(model_label_, 0, Qt::AlignCenter);
  main_layout->addLayout(icon_label_layout);
  system_info_widget_ = new SystemInfoWidget(header.system_info, this);
  system_info_widget_->setSizePolicy(QSizePolicy::Minimum,
                                     QSizePolicy::Preferred);
  main_layout->addWidget(system_info_widget_);
}

ResultTableWidget::ResultTableWidget(const QVector<HeaderInfo>& headers,
                                     QWidget* parent)
    : QWidget(parent), col_number_(headers.size() + 1) {
  outer_layout_ = new QVBoxLayout(this);
  outer_layout_->setContentsMargins(0, 0, 0, 0);
  widget_frame_ = new QFrame();
  widget_frame_->setFrameStyle(QFrame::NoFrame);
  main_layout_ = new QGridLayout(widget_frame_);
  outer_layout_->addWidget(widget_frame_);
  main_layout_->setSizeConstraint(QLayout::SetMinimumSize);
  main_layout_->setContentsMargins(0, 0, 0, 0);
  main_layout_->setVerticalSpacing(0);
  outer_layout_->addStretch();
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  SetHeaders(headers);
}

void ResultTableWidget::AddRow(const QStringList& row_data) {
  AddRow(row_data, false);
}
void ResultTableWidget::AddBoldRow(const QStringList& row_data, bool bold) {
  AddRow(row_data, false, false, bold);
}

void ResultTableWidget::AddTitle(const QString& main_text,
                                 const QString& sub_text) {
  bool is_section = false;
  QString title_label =
      QString("<div style=\"font-weight: bold; font-size:20px\">%1</div>")
          .arg(main_text);
  if (!sub_text.isEmpty()) {
    if (!main_text.isEmpty()) {
      is_section = true;
      AddRow({""}, false, false);
      title_label += QString(
                         "<div style=\"font-weight: bold; padding-top:10px; "
                         "font-size:16px\"> <br> %1</div>")
                         .arg(sub_text);
    } else {
      title_label =
          QString("<div style=\"font-weight: bold; font-size:16px\">%1</div>")
              .arg(sub_text);
    }
  }

  AddRow({title_label}, true, is_section);
}

void ResultTableWidget::SetText(int row, int col, const QString& text) {
  if (row < row_widget_list_.size() && col < row_widget_list_[row].size()) {
    static_cast<CellWidget*>(row_widget_list_[row][col])->SetText(text);
  }
}

void ResultTableWidget::AddRow(const QStringList& text_list, bool is_title,
                               bool is_section, bool is_bold) {
  QVector<QWidget*> row_widgets;
  for (int i = 0; i < text_list.size(); ++i) {
    row_widgets.push_back(new CellWidget(text_list[i], i, is_bold));
  }
  for (int i = text_list.size(); i < col_number_; ++i) {
    row_widgets.push_back(new CellWidget());
  }
  row_widget_list_.push_back(row_widgets);
  if (is_title) title_rows_.push_back(row_widget_list_.size() - 1);
  if (is_section) section_rows_.push_back(row_widget_list_.size() - 1);

  for (int i = 0; i < row_widgets.size(); ++i) {
    main_layout_->addWidget(row_widgets[i], row_widget_list_.size(), i);
  }
}

void ResultTableWidget::SetHeaders(const QVector<HeaderInfo>& header_list) {
  header_widget_list_.clear();
  header_widget_list_.push_back(new CellWidget());
  for (const auto& header : header_list) {
    header_widget_list_.push_back(new HeaderCell(header, this));
  }
  for (int i = 0; i < header_widget_list_.size(); ++i) {
    main_layout_->addWidget(header_widget_list_[i], 0, i);
    main_layout_->setColumnStretch(i, 1);
  }
}

void ResultTableWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);

  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0, COLUMN_GRADIENT_COLOR_0);
  gradient.setColorAt(1, COLUMN_GRADIENT_COLOR_1);
  painter.setBrush(gradient);

  auto rects = GetColumnRects();
  for (const auto& rect : rects)
    painter.drawRoundedRect(rect, COLUMN_BORDER_RADIUS, COLUMN_BORDER_RADIUS);

  painter.setBrush(ROW_COLOR);

  rects = GetTitleRowRects();
  for (const auto& rect : rects) painter.drawRect(rect);

  QPen pen(SECTION_BORDER_COLOR, SECTION_BORDER_WIDTH);
  painter.setPen(pen);
  painter.setBrush(Qt::transparent);
  rects = GetSectionRects();
  for (const auto& rect : rects) {
    painter.drawRoundedRect(rect, COLUMN_BORDER_RADIUS, COLUMN_BORDER_RADIUS);
  }

  QWidget::paintEvent(event);
}

QVector<QRect> ResultTableWidget::GetColumnRects() const {
  QVector<QRect> column_rects;
  for (int col = 0; col < col_number_; ++col) {
    QRect rect;
    for (const auto& row : row_widget_list_)
      rect = rect.united(row.at(col)->geometry());
    if (!header_widget_list_.isEmpty())
      rect = rect.united(header_widget_list_.at(col)->geometry());
    rect.setHeight(height());
    column_rects.push_back(rect);
  }
  return column_rects;
}

QRect ResultTableWidget::GetRowGeometry(int row) const {
  QRect rect;
  for (const auto& cell : row_widget_list_[row])
    rect = rect.united(cell->geometry());
  return rect;
}

QVector<QRect> ResultTableWidget::GetTitleRowRects() const {
  QVector<QRect> row_rects;
  for (const auto& row : title_rows_) {
    row_rects.push_back(GetRowGeometry(row));
  }
  return row_rects;
}

QVector<QRect> ResultTableWidget::GetSectionRowRects() const {
  QVector<QRect> row_rects;
  for (const auto& row : section_rows_) {
    row_rects.push_back(GetRowGeometry(row));
  }
  return row_rects;
}

QRect ResultTableWidget::GetSectionAreas(const QRect& fisrt_rect,
                                         const QRect& second_react) const {
  auto top = std::min(fisrt_rect.top(), second_react.top());
  auto bottom = std::max(fisrt_rect.bottom(), second_react.top());
  auto left = std::min(fisrt_rect.left(), second_react.left());
  auto right = std::max(fisrt_rect.right(), second_react.right());
  auto height = bottom - top;
  auto width = right - left;
  return QRect(left, top - SECTION_TOP_MARGIN, width,
               height - SPACE_BETWEEN_SECTIONS);
}

QVector<QRect> ResultTableWidget::GetSectionRects() const {
  QVector<QRect> section_areas;
  auto overview_rects = GetSectionRowRects();
  // Cover the last section
  if (overview_rects.size()) {
    QRect last_rect = GetRowGeometry(row_widget_list_.size() - 1);
    last_rect.setY(last_rect.bottom() + SPACE_BETWEEN_SECTIONS +
                   SECTION_TOP_MARGIN);
    overview_rects.append(last_rect);
  }
  for (int i = 0; i < overview_rects.size() - 1; i++) {
    section_areas.append(
        GetSectionAreas(overview_rects[i], overview_rects[i + 1]));
  }
  return section_areas;
}

SystemInfoWidget::SystemInfoWidget(
    const QList<QPair<QString, QString>>& sys_info_records, QWidget* parent)
    : QWidget(parent) {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(5, 10, 5, 10);
  int max_key_label_width = 0;
  for (const auto& record : sys_info_records) {
    SystemRecordWidget* record_widget =
        new SystemRecordWidget(record.first, record.second);
    info_records_widgets_.append(record_widget);
    main_layout->addWidget(record_widget);
    int width = record_widget->GetKeyLabel()->sizeHint().width();
    if (width > max_key_label_width) max_key_label_width = width;
  }
  for (const auto& record_widget : info_records_widgets_)
    record_widget->GetKeyLabel()->setMinimumWidth(max_key_label_width);
  main_layout->addStretch();
}

SystemRecordWidget::SystemRecordWidget(const QString& title,
                                       const QString& value, QWidget* parent)
    : QWidget(parent), value_text_(value) {
  auto* main_layout = new QHBoxLayout(this);
  main_layout->setSizeConstraint(QLayout::SetMinimumSize);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(4);
  auto* left_h_layout = new QHBoxLayout();
  auto* bullet_label = new QLabel(this);
  bullet_label->setObjectName("bullet_rounded");
  bullet_label->setFixedSize(QSize(8, 8));
  key_label_ = new QLabel(title, this);
  key_label_->setObjectName("history_sys_info_key_label");
  value_label_ = new ElidedLabel(value, this);
  value_label_->SetPreferredLineCount(2);
  left_h_layout->addWidget(bullet_label);
  left_h_layout->addWidget(key_label_);
  auto* left_v_layout = new QVBoxLayout();
  left_v_layout->addLayout(left_h_layout);
  left_v_layout->addStretch();
  main_layout->addLayout(left_v_layout);
  main_layout->addWidget(value_label_, 1);
  setToolTip(value);
}

}  // namespace custom_widgets