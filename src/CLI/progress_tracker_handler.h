#ifndef PROGRESS_TRACKER_HANDLER_H_
#define PROGRESS_TRACKER_HANDLER_H_

#include <chrono>
#include <memory>

#include "progress_tracker.h"

namespace cli {

class ProgressTrackerHandler : public cil::ProgressTracker::HandlerBase {
 public:
  explicit ProgressTrackerHandler(std::atomic<bool>& interrupt);

  ~ProgressTrackerHandler() final = default;
  ProgressTrackerHandler(const ProgressTrackerHandler&) = default;

  void StartTracking(cil::ProgressTracker& tracker) override;
  void StopTracking(cil::ProgressTracker& tracker) override;

  void Update(cil::ProgressTracker& tracker) override;
  bool RequestInterrupt(cil::ProgressTracker& tracker) override;

  std::atomic<bool>& GetInterrupt() override;

 private:
  void DisplayCurrentTaskAndOverallProgress(cil::ProgressTracker& tracker);

  std::string FormatToConsoleWidth(const std::string& stream_string,
                                   const std::string& description) const;
  std::string ReplaceDescriptionSubString(const std::string& stream_string,
                                          const std::string& description) const;

  static const std::string kRotatingCursor;
  static const std::string kDescriptionTemplate;

  size_t rotating_cursor_position_ = 0;

  bool interactive_console_mode_ = false;
  int console_width_ = 0;

  std::atomic<bool>& interrupt_;
};

}  // namespace cli

#endif  // !PROGRESS_TRACKER_H_
