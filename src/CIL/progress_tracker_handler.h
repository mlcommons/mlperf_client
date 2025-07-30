#ifndef PROGRESS_TRACKER_HANDLER_H_
#define PROGRESS_TRACKER_HANDLER_H_

#include <chrono>
#include <functional>
#include <memory>

#include "progress_tracker.h"

namespace cil {

class ProgressTrackerHandler : public cil::ProgressTracker::HandlerBase {
 public:
  explicit ProgressTrackerHandler(std::atomic<bool>& interrupt);

  ~ProgressTrackerHandler() = default;
  ProgressTrackerHandler(const ProgressTrackerHandler&) = default;

  void StartTracking(cil::ProgressTracker& tracker) override;
  void StopTracking(cil::ProgressTracker& tracker) override;

  void Update(cil::ProgressTracker& tracker) override;
  bool RequestInterrupt(cil::ProgressTracker& tracker) override;

  std::atomic<bool>& GetInterrupt() override;

  void SetForceAcceptInterruptRequest(bool accept);

 private:
  void DisplayCurrentTaskAndOverallProgress(cil::ProgressTracker& tracker);

  std::string FormatToConsoleWidth(const std::string& stream_string,
                                   const std::string& description) const;
  std::string ReplaceDescriptionSubString(const std::string& stream_string,
                                          const std::string& description) const;
  void LogCallback(const std::string& message, bool move_start = false,
                   bool move_up = false, bool move_down = false);

  static const std::string kRotatingCursor;
  static const std::string kDescriptionTemplate;

  size_t rotating_cursor_position_ = 0;

 protected:
  bool interactive_console_mode_ = false;
  int console_width_ = 0;
  std::atomic<bool>& interrupt_;
  bool force_accept_interrupt_request_ = false;
  std::function<void(const std::string&, bool, bool, bool)> logger_callback_;
};

}  // namespace cil

#endif  // !PROGRESS_TRACKER_H_
