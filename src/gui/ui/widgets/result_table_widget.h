#ifndef CUSTOM_TABLE_WIDGET_H_
#define CUSTOM_TABLE_WIDGET_H_

#include <QDateTime>
#include <QWidget>

#include "../../core/types.h"
#include "elided_label.h"

class QLabel;
class QPaintEvent;
class QGridLayout;
class QVBoxLayout;
class QFrame;

namespace custom_widgets {

struct HeaderInfo {
  QString model_name;
  QString ep;
  QString icon;
  QString device;
  QDateTime datetime;
  bool tested_by_ml_commons;
  bool is_experimental;
  QList<QPair<QString, QString>>  system_info;
};

class CellWidget : public QWidget {
 public:
  CellWidget(const QString& text = "", int index = 0, bool bold = false,
             QWidget* parent = nullptr);
  void SetText(const QString& text);

 private:
  QLabel* label_;
};

class SystemRecordWidget : public QWidget {
 public:
  SystemRecordWidget(const QString& title, const QString& value,
                     QWidget* parent = nullptr);
  QLabel* GetKeyLabel() const { return key_label_; }

 private:
  ElidedLabel* value_label_;
  QLabel* key_label_;
  QString value_text_;
};

class SystemInfoWidget : public QWidget {
 public:
  SystemInfoWidget(const QList<QPair<QString, QString>>& system_info,
                   QWidget* parent = nullptr);

 private:
  QList<SystemRecordWidget*> info_records_widgets_;
};

class HeaderCell : public QWidget {
 public:
  HeaderCell(const HeaderInfo& header, QWidget* parent = nullptr);

 private:
  QLabel* info_icon_label_;
  QLabel* tested_by_ml_commons_label_;
  QLabel* date_icon_label_;
  QLabel* date_label_;
  QLabel* time_icon_label_;
  QLabel* time_label_;
  QLabel* model_label_;
  QLabel* ep_label_;
  QLabel* device_label_;
  QLabel* device_icon_label_;
  SystemInfoWidget* system_info_widget_;
};

class ResultTableWidget : public QWidget {
 public:
  ResultTableWidget(const QVector<HeaderInfo>& headers,
                    QWidget* parent = nullptr);

  void AddRow(const QStringList& row_data = {});
  void AddBoldRow(const QStringList& row_data, bool bold);
  void AddTitle(const QString& main_text = "", const QString& sub_text = "");
  void SetText(int row, int col, const QString& text);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void AddRow(const QStringList& text_list, bool is_title,
              bool is_section = false, bool is_bold = false);
  void SetHeaders(const QVector<HeaderInfo>& header_list);

  QVector<QRect> GetColumnRects() const;
  QRect GetRowGeometry(int row) const;
  QVector<QRect> GetTitleRowRects() const;

  QVector<QRect> GetSectionRowRects() const;
  QRect GetSectionAreas(const QRect& fisrt_rect,
                        const QRect& second_rect) const;
  QVector<QRect> GetSectionRects() const;
  QVBoxLayout* outer_layout_;
  QFrame* widget_frame_;
  QGridLayout* main_layout_;
  QVector<QWidget*> header_widget_list_;
  QVector<int> section_rows_;
  QVector<QVector<QWidget*>> row_widget_list_;
  QVector<int> title_rows_;
  int col_number_;
};

}  // namespace custom_widgets

#endif  // CUSTOM_TABLE_WIDGET_H_