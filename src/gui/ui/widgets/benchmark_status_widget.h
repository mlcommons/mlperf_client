#ifndef BENCHMARK_STATUS_WIDGET_H_
#define BENCHMARK_STATUS_WIDGET_H_

#include <QDialog>

class ElidedLabel;
class QLabel;
class QPushButton;

namespace gui {
struct EPBenchmarkStatus;
struct BenchmarkStatus;
}  // namespace gui

class BenchmarkStatusCardWidget : public QWidget {
  Q_OBJECT
 public:
  explicit BenchmarkStatusCardWidget(const gui::EPBenchmarkStatus& status,
                                     QWidget* parent = nullptr);
 private slots:
  void OnShowMoreClicked();

 private:
  QWidget* CreateStatusLabel(bool success);

  bool show_more_;
  ElidedLabel* error_label_;
  QPushButton* show_more_button_;
};

/**
 * @class BenchmarkStatusWidget
 * @brief Dialog for visualizing benchmark status, including data download
 * status or the full benchmark execution.
 */
class BenchmarkStatusWidget : public QDialog {
  Q_OBJECT
 public:
  explicit BenchmarkStatusWidget(const QString& action_type,
                                 const gui::BenchmarkStatus& status,
                                 QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QPushButton* close_button_;
};

#endif  // BENCHMARK_STATUS_WIDGET_H_
