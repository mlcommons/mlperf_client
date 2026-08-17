#ifndef LOADING_WIDGET_CONTROLLER_H_
#define LOADING_WIDGET_CONTROLLER_H_

#include <QElapsedTimer>
#include <QObject>

class LoadingWidget;

namespace gui {
namespace controllers {

class LoadingWidgetController : public QObject {
  Q_OBJECT
 public:
  explicit LoadingWidgetController(QObject* parent = nullptr);

  /// Wires the widget. Call with nullptr to detach and reset state.
  void SetWidget(LoadingWidget* widget);

 public slots:
  /// Coarse per-EP progress (0-100) during device enumeration.
  void UpdateEnumerationProgress(int percent);
  /// Progress through the upfront HEAD-only size pre-pass.
  void UpdateSizingProgress(int configs_done, int configs_total);
  /// Cumulative byte-level download progress across all EPs.
  void UpdateDownloadProgress(qint64 downloaded_bytes, qint64 total_bytes);
  /// Transitions to the device enumeration phase.
  void StartPreparationPhase();

 private:
  enum class Phase { kInitial, kSizing, kDownloading, kPreparation };

  LoadingWidget* widget_ = nullptr;

  /// Measures download speed for ETA calculation; restarted on first byte.
  QElapsedTimer progress_timer_;
  Phase phase_ = Phase::kInitial;
  bool download_active_ = false;
};

}  // namespace controllers
}  // namespace gui

#endif  // LOADING_WIDGET_CONTROLLER_H_
