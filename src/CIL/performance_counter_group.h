#pragma once

#include <pdh.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cil {

class PerformanceCounterDataBase;
class PerformanceCounterGroup {
 public:
  PerformanceCounterGroup(
      const std::string& name,
      std::unique_ptr<PerformanceCounterDataBase> counter_data)
      : name_(name),
        counter_group_start_index_(-1),
        counter_data_(std::move(counter_data)) {}
  virtual ~PerformanceCounterGroup() = default;

  // labels should be unique
  void StartCounterGroup();
  bool EndCounterGroup(const std::string& label);
  bool AddCounter(const std::string& label, const std::string& units,
                  const std::string& counter_path, double scale = 1.,
                  const std::wstring& subtype = std::wstring());

  PerformanceCounterDataBase* GetData() const { return counter_data_.get(); }
  double GetValue(int device_index, const std::string& label,
                  bool* exists = nullptr) const;

  bool Format();
  void Print();

 protected:
  friend class PerformanceQuery;
  struct Counter {
    double scale;
    std::string label;
    std::string units;
    std::string counter_path;
    std::wstring counter_subtype;
    //
    PDH_HCOUNTER counter_handle = INVALID_HANDLE_VALUE;
  };

  struct CounterGroup {
    std::string label;
    std::vector<int> counter_ids;
  };

  virtual bool Format(int index);

  //
  std::string name_;
  std::vector<Counter> counters_;
  std::vector<CounterGroup> counter_groups_;
  std::unordered_map<std::string, std::pair<bool, int>> counters_map_;
  std::unique_ptr<PerformanceCounterDataBase> counter_data_;

  //
  int counter_group_start_index_;
};

//
//
class PerformanceCounterArrayGroup : public PerformanceCounterGroup {
 public:
  PerformanceCounterArrayGroup(
      const std::string& name,
      std::unique_ptr<PerformanceCounterDataBase> counter_data)
      : PerformanceCounterGroup(name, std::move(counter_data)) {}

 private:
  bool Format(int index) override;
};

}  // namespace cil