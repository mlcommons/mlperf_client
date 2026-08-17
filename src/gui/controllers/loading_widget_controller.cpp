#include "loading_widget_controller.h"

#include <algorithm>

#include "ui/widgets/loading_widget.h"

// Wait for a settled download rate before showing ETA to avoid noisy initial
// estimates.
static constexpr qint64 LOADING_ETA_WARMUP_MS = 3000;

// Progress bar allocation across the three phases.
static constexpr int LOADING_SIZING_END_PERCENT = 5;
static constexpr int LOADING_DOWNLOAD_END_PERCENT = 75;

namespace gui {
namespace controllers {

LoadingWidgetController::LoadingWidgetController(QObject* parent)
    : QObject(parent) {}

void LoadingWidgetController::SetWidget(LoadingWidget* widget) {
  widget_ = widget;
  if (!widget_) {
    phase_ = Phase::kInitial;
    download_active_ = false;
    return;
  }
  widget_->SetMessage("Downloading benchmark assets");
}

void LoadingWidgetController::UpdateEnumerationProgress(int percent) {
  if (!widget_) return;
  if (phase_ == Phase::kSizing || phase_ == Phase::kDownloading) return;

  percent = std::clamp(percent, 0, 100);
  // Map per-EP coarse progress into the LOADING_DOWNLOAD_END_PERCENT..100
  // sub-range during preparation so the bar advances smoothly.
  const int displayed_percent =
      (phase_ == Phase::kPreparation)
          ? LOADING_DOWNLOAD_END_PERCENT +
                percent * (100 - LOADING_DOWNLOAD_END_PERCENT) / 100
          : percent;
  widget_->SetProgressPercent(displayed_percent);

  // Device enumeration has no byte-level ETA.
  if (phase_ == Phase::kPreparation) {
    widget_->ShowEtaPlaceholder();
    return;
  }

  // Linear ETA fallback when no byte-level progress is available (e.g. all
  // assets are already cached).
  if (percent <= 0) {
    widget_->ShowEtaPlaceholder();
  } else if (percent >= 100) {
    widget_->ClearEta();
  } else {
    const qint64 elapsed_ms = progress_timer_.elapsed();
    const qint64 eta_ms = static_cast<qint64>(static_cast<double>(elapsed_ms) *
                                              (100 - percent) / percent);
    widget_->SetEtaMs(eta_ms);
  }
}

void LoadingWidgetController::UpdateSizingProgress(int configs_done,
                                                   int configs_total) {
  if (!widget_) return;
  if (configs_total <= 0) return;
  phase_ = Phase::kSizing;

  // Sizing fills 0..LOADING_SIZING_END_PERCENT.
  const int percent =
      std::clamp(configs_done * LOADING_SIZING_END_PERCENT / configs_total, 0,
                 LOADING_SIZING_END_PERCENT);
  widget_->SetProgressPercent(percent);
  // No ETA during the HEAD sweep; show placeholder to avoid an empty line.
  widget_->ShowEtaPlaceholder();
}

void LoadingWidgetController::UpdateDownloadProgress(qint64 downloaded_bytes,
                                                     qint64 total_bytes) {
  if (!widget_) return;

  // total_bytes==0 means nothing to download; skip straight to preparation.
  if (total_bytes <= 0) {
    phase_ = Phase::kPreparation;
    widget_->SetProgressPercent(LOADING_DOWNLOAD_END_PERCENT);
    widget_->ShowEtaPlaceholder();
    return;
  }
  phase_ = Phase::kDownloading;

  // downloaded_bytes is cumulative across all EPs; total_bytes is fixed, so
  // the mapped percentage rises monotonically.
  constexpr int kDownloadSpan =
      LOADING_DOWNLOAD_END_PERCENT - LOADING_SIZING_END_PERCENT;
  const int percent = static_cast<int>(std::clamp<qint64>(
      LOADING_SIZING_END_PERCENT +
          downloaded_bytes * kDownloadSpan / total_bytes,
      LOADING_SIZING_END_PERCENT, LOADING_DOWNLOAD_END_PERCENT));
  widget_->SetProgressPercent(percent);

  if (downloaded_bytes >= total_bytes) {
    // Downloads finished — suppress ETA for the upcoming enumeration phase.
    phase_ = Phase::kPreparation;
    widget_->ShowEtaPlaceholder();
    return;
  }

  // Start the rate clock when bytes first begin to arrive.
  if (!download_active_ && downloaded_bytes > 0) {
    download_active_ = true;
    progress_timer_.restart();
  }

  const qint64 elapsed_ms = progress_timer_.elapsed();
  if (download_active_ && downloaded_bytes > 0 &&
      elapsed_ms >= LOADING_ETA_WARMUP_MS) {
    // Pass a fresh average-rate estimate to the widget's countdown timer.
    const qint64 eta_ms = static_cast<qint64>(
        static_cast<double>(total_bytes - downloaded_bytes) * elapsed_ms /
        downloaded_bytes);
    widget_->SetEtaMs(eta_ms);
  } else {
    widget_->ShowEtaPlaceholder();
  }
}

void LoadingWidgetController::StartPreparationPhase() {
  if (!widget_) return;
  phase_ = Phase::kPreparation;
  // Snap bar to preparation sub-range start so UpdateEnumerationProgress
  // fills it monotonically.
  widget_->SetProgressPercent(LOADING_DOWNLOAD_END_PERCENT);
  widget_->ShowEtaPlaceholder();
  widget_->SetMessage("Detecting devices");
}

}  // namespace controllers
}  // namespace gui
