#ifndef IO_REPORT_READER_H_
#define IO_REPORT_READER_H_

#include <CoreFoundation/CoreFoundation.h>

#include <utility>
#include <chrono>

struct IOReportSubscription;
typedef struct IOReportSubscription* IOReportSubscriptionRef;
typedef CFDictionaryRef IOReportSampleRef;

class IOReportReader {
 public:
  IOReportReader();
  ~IOReportReader();

  std::pair<double, double> GetGpuAndNpuUsage();

 private:
  bool isValid() const;

  IOReportSubscriptionRef subscription_;
  CFMutableDictionaryRef channels_;
  CFMutableDictionaryRef subscribed_channels_;
  CFDictionaryRef previous_sample_;
    
  std::chrono::steady_clock::time_point previous_sample_time_;

};

#endif  // IO_REPORT_READER_H_
