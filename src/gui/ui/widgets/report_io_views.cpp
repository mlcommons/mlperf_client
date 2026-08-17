#include "report_io_views.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "io_view_widgets.h"

namespace custom_widgets {

namespace {

QWidget* MakePromptBlock(int entry_index, const QString& prompt_text,
                         bool agentic) {
  QString icon = agentic ? ":/icons/resources/icons/io_chat_user.png"
                         : ":/icons/resources/icons/io_prompt.png";
  QString hdr = agentic ? "User" : QString("Prompt %1").arg(entry_index + 1);
  return new TextBlockWidget(hdr, prompt_text, icon);
}

QWidget* MakeOutputBlock(int entry_index, const QString& output_text,
                         const QString& image_path, bool agentic,
                         bool is_image_benchmark) {
  QString icon = agentic ? ":/icons/resources/icons/io_chat_agent.png"
                         : ":/icons/resources/icons/io_response.png";
  QString hdr = agentic ? "Agent" : QString("Response %1").arg(entry_index + 1);
  if (is_image_benchmark) return new ImageBlockWidget(hdr, image_path, icon);
  return new TextBlockWidget(hdr, output_text, icon);
}

IOEntry ExtractIOEntry(const cil::BenchmarkResult& r, int idx) {
  IOEntry entry;
  entry.index = idx;
  if (idx < static_cast<int>(r.input_prompts.size()))
    entry.prompt = QString::fromStdString(r.input_prompts[idx]);
  if (idx < static_cast<int>(r.output.size()))
    entry.output = QString::fromStdString(r.output[idx]);
  if (r.is_image_benchmark) {
    if (idx < static_cast<int>(r.output_image_paths.size()))
      entry.image_path = QString::fromStdString(r.output_image_paths[idx]);
  } else if (entry.output.isEmpty()) {
    entry.output = "[no response captured]";
  }
  return entry;
}

std::vector<int> EntryIndicesForCategory(const cil::BenchmarkResult& r,
                                         const QString& category) {
  std::vector<int> indices;
  for (size_t i = 0; i < r.input_categories.size(); ++i) {
    if (QString::fromStdString(r.input_categories[i]) == category)
      indices.push_back(static_cast<int>(i));
  }
  return indices;
}

bool IsAgenticCategory(const cil::BenchmarkResult& r, const QString& category) {
  if (auto it = r.performance_results.find(category.toStdString());
      it != r.performance_results.end()) {
    return it->second.is_agentic;
  }
  return false;
}

}  // namespace

bool ResultsShareInputs(const std::vector<cil::BenchmarkResult>& results,
                        const QString& category) {
  std::vector<std::vector<std::string>> per_result_prompts;
  for (const auto& r : results) {
    std::vector<std::string> prompts;
    auto indices = EntryIndicesForCategory(r, category);
    if (indices.empty()) continue;
    for (int i : indices) {
      if (i < static_cast<int>(r.input_prompts.size()))
        prompts.push_back(r.input_prompts[i]);
    }
    per_result_prompts.push_back(std::move(prompts));
  }
  if (per_result_prompts.size() < 2) return false;
  for (size_t i = 1; i < per_result_prompts.size(); ++i)
    if (per_result_prompts[i] != per_result_prompts[0]) return false;

  return true;
}

// ---------------------------------------------------------------------------
// IOView
// ---------------------------------------------------------------------------

IOView::IOView(QWidget* parent) : QWidget(parent) {
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(8, 6, 8, 8);
  layout_->setSpacing(6);
}

void IOView::EnsureBuilt() {
  if (built_) return;
  built_ = true;
  Build();
}

// ---------------------------------------------------------------------------
// PerResultIOView — one per (result, category), used when prompts differ.
// ---------------------------------------------------------------------------

PerResultIOView::PerResultIOView(const cil::BenchmarkResult& result,
                                 const QString& category, QWidget* parent)
    : IOView(parent),
      agentic_(IsAgenticCategory(result, category)),
      is_image_(result.is_image_benchmark) {
  for (int idx : EntryIndicesForCategory(result, category))
    entries_.append(ExtractIOEntry(result, idx));
}

void PerResultIOView::Build() {
  if (entries_.empty()) {
    QLabel* placeholder = new QLabel("[no prompts captured]");
    placeholder->setProperty("class", "small_normal_label");
    layout_->addWidget(placeholder);
    return;
  }
  for (const IOEntry& entry : entries_) {
    layout_->addWidget(MakePromptBlock(entry.index, entry.prompt, agentic_));
    layout_->addWidget(MakeOutputBlock(entry.index, entry.output,
                                       entry.image_path, agentic_, is_image_));
  }
}

// ---------------------------------------------------------------------------
// MergedIOView — shared prompts on top, per-column outputs below.
// ---------------------------------------------------------------------------

MergedIOView::MergedIOView(const std::vector<cil::BenchmarkResult>& results,
                           const QString& category, QWidget* parent)
    : IOView(parent) {
  // Per-result entry indices for this category, computed once. The first
  // result with prompts contributes the shared prompt list.
  int reference = -1;
  std::vector<std::vector<int>> indices;
  indices.reserve(results.size());
  for (size_t i = 0; i < results.size(); ++i) {
    indices.push_back(EntryIndicesForCategory(results[i], category));
    if (reference < 0 && !indices[i].empty()) reference = static_cast<int>(i);
  }
  if (reference < 0) return;  // no prompts captured; Build() shows a message

  agentic_ = IsAgenticCategory(results[reference], category);
  is_image_ = results[reference].is_image_benchmark;

  for (int idx : indices[reference])
    prompts_.append(ExtractIOEntry(results[reference], idx).prompt);

  columns_.reserve(results.size());
  for (size_t i = 0; i < results.size(); ++i) {
    QList<IOEntry> column;
    column.reserve(indices[i].size());
    for (int idx : indices[i]) column.append(ExtractIOEntry(results[i], idx));
    columns_.append(std::move(column));
  }
}

void MergedIOView::Build() {
  if (prompts_.empty()) {
    QLabel* placeholder = new QLabel("[no prompts captured]");
    placeholder->setProperty("class", "small_normal_label");
    layout_->addWidget(placeholder);
    return;
  }

  for (int i = 0; i < prompts_.size(); ++i) {
    // Shared prompt (full width within this widget).
    layout_->addWidget(MakePromptBlock(i, prompts_[i], agentic_));

    // Per-column outputs in N internal cells (equal stretch). For 1 result
    // the row collapses to a single output spanning the whole width.
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    layout_->addLayout(row);

    for (const QList<IOEntry>& column : columns_) {
      QWidget* col_widget = new QWidget();
      auto* col_layout = new QVBoxLayout(col_widget);
      col_layout->setContentsMargins(0, 0, 0, 0);
      col_layout->setSpacing(4);

      if (i < column.size()) {
        const IOEntry& entry = column[i];
        col_layout->addWidget(MakeOutputBlock(i, entry.output, entry.image_path,
                                              agentic_, is_image_));
      } else {
        QLabel* placeholder = new QLabel("[no response captured]");
        placeholder->setProperty("class", "small_normal_label");
        col_layout->addWidget(placeholder);
      }
      row->addWidget(col_widget, 1);
    }
  }
}

// ---------------------------------------------------------------------------
// CategoryTitleToggle
// ---------------------------------------------------------------------------

CategoryTitleToggle::CategoryTitleToggle(const QString& category,
                                         QWidget* parent)
    : QPushButton(parent) {
  setObjectName("category_title_toggle");
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
  setText(category);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  auto applyStateFn = [this](bool expanded) {
    setIcon(QIcon(expanded ? ":/icons/resources/icons/chevron_gray_down.png"
                           : ":/icons/resources/icons/chevron_gray_right.png"));
    setToolTip(expanded ? "Hide prompts and responses"
                        : "Show prompts and responses for this category");
  };
  applyStateFn(isChecked());
  connect(this, &QPushButton::toggled, this, applyStateFn);
}

}  // namespace custom_widgets
