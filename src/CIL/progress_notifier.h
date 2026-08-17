#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace cil {

// (percent_completed, downloaded_bytes, total_bytes)
using ProgressCallback = std::function<void(int, uint64_t, uint64_t)>;

class ProgressNotifier {
 public:
  ProgressNotifier() : last_notified_percent_(0), notify_frequency_(1) {}

  ProgressNotifier(int notify_frequency)
      : last_notified_percent_(0), notify_frequency_(notify_frequency) {}

 public:
  void operator()(int percents_completed, uint64_t downloaded_bytes,
                  uint64_t total_bytes,
                  const ProgressCallback& progress_callback) {
    if (percents_completed > last_notified_percent_ &&
        !(percents_completed % notify_frequency_)) {
      progress_callback(last_notified_percent_ = percents_completed,
                        downloaded_bytes, total_bytes);
    }
  }

  void reset() { last_notified_percent_ = 0; }

 private:
  int last_notified_percent_;
  int notify_frequency_;
};

}  // namespace cil
