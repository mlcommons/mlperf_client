#pragma once

#include <pdh.h>

#include <memory>
#include <vector>

namespace cil {

class PerformanceCounterGroup;
class PerformanceQuery {
 public:
  PerformanceQuery();
  ~PerformanceQuery();

  bool Open();
  bool Close();
  bool Query();

  bool AddCounterGroup(std::shared_ptr<PerformanceCounterGroup> counter_group);
  bool FormatCounters();

 private:
  PDH_HQUERY query_handle_;
  std::vector<std::shared_ptr<PerformanceCounterGroup>> counter_groups_;
};

}  // namespace cil