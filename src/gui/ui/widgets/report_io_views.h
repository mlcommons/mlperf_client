#ifndef REPORT_IO_VIEWS_H_
#define REPORT_IO_VIEWS_H_

#include <QList>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <vector>

#include "../CIL/benchmark_result.h"

class QVBoxLayout;

namespace custom_widgets {

// Prompt/output/image for a single I/O entry; `index` is its position in the
// source result. A missing text response holds a "[no response captured]"
// placeholder.
struct IOEntry {
  int index = 0;
  QString prompt;
  QString output;
  QString image_path;
};

/**
 * @brief Base for lazily-built per-category I/O views. Owns a vertical content
 *        layout and the build-once guard; subclasses extract their data in the
 *        constructor and populate the layout in Build().
 */
class IOView : public QWidget {
  Q_OBJECT
 public:
  // Builds the content on the first call; later calls are no-ops.
  void EnsureBuilt();

 protected:
  explicit IOView(QWidget* parent);
  virtual void Build() = 0;

  QVBoxLayout* layout_ = nullptr;

 private:
  bool built_ = false;
};

/**
 * @brief Per-(result, category) prompts/outputs view, built lazily. Used
 *        when results in the same column have differing prompts so each
 *        column renders its own input/output stack.
 */
class PerResultIOView : public IOView {
  Q_OBJECT
 public:
  PerResultIOView(const cil::BenchmarkResult& result, const QString& category,
                  QWidget* parent = nullptr);

 protected:
  void Build() override;

 private:
  QList<IOEntry> entries_;
  bool agentic_ = false;
  bool is_image_ = false;
};

/**
 * @brief Per-category view that shows a single shared prompt block per turn
 *        followed by per-column outputs. Used when all entries in the
 *        report ran the same prompts (the common case for comparing EPs on
 *        the same scenario).
 */
class MergedIOView : public IOView {
  Q_OBJECT
 public:
  MergedIOView(const std::vector<cil::BenchmarkResult>& results,
               const QString& category, QWidget* parent = nullptr);

 protected:
  void Build() override;

 private:
  QList<QString> prompts_;         // shared prompt per turn
  QList<QList<IOEntry>> columns_;  // per result, its entries
  bool agentic_ = false;
  bool is_image_ = false;
};

/**
 * @brief Title widget shown in column 0 of a category section. Renders the
 *        category name preceded by a chevron and is itself the toggle that
 *        controls the matching I/O row visibility.
 */
class CategoryTitleToggle : public QPushButton {
  Q_OBJECT
 public:
  CategoryTitleToggle(const QString& category, QWidget* parent = nullptr);
};

/**
 * @brief Decide whether all entries' prompts for the given category are
 *        identical (so the I/O row can use the merged shared-input
 *        layout). Empty-prompt entries are ignored.
 */
bool ResultsShareInputs(const std::vector<cil::BenchmarkResult>& results,
                        const QString& category);

}  // namespace custom_widgets

#endif  // REPORT_IO_VIEWS_H_
