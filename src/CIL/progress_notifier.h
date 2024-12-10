#pragma once

#include <functional>
#include <string>


namespace cil {

typedef std::function<void(int)> ProgressCallback;

class ProgressNotifier {
 public:
  ProgressNotifier(): last_notified_percent_(0), notify_frequency_(1) {}

  ProgressNotifier(int notify_frequency) : last_notified_percent_(0), notify_frequency_(notify_frequency) {}

 public:
  void operator()(
      int percents_completed, const ProgressCallback& progress_callback) {
    if (percents_completed > last_notified_percent_ && !(percents_completed % notify_frequency_)) {
      progress_callback(last_notified_percent_ = percents_completed);
    }
  }

  void reset() { last_notified_percent_ = 0; }

 private:
  int last_notified_percent_;
  int notify_frequency_;
};

}  // namespace cil
