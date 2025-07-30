#ifndef SYSTEM_INFO_PROVIDER_WINDOWS_H_
#define SYSTEM_INFO_PROVIDER_WINDOWS_H_

#include <memory>
#include <string>

#include "system_info_provider.h"

struct IWbemLocator;
struct IWbemServices;

namespace cil {

class PerformanceQuery;
class PerformanceCounterGroup;
class PerformanceCounterArrayGroup;

class SystemInfoProviderWindows : public SystemInfoProvider {
 public:
  SystemInfoProviderWindows();
  ~SystemInfoProviderWindows() override;

 protected:
  void FetchInfo() override;
  void FetchCpuInfo() override;
  void FetchGpuInfo() override;
  void FetchNpuInfo() override;
  void FetchSystemMemoryInfo() override;
  void FetchOsInfo() override;

  void FetchPerformanceInfo() override;

 private:
  void InitializeWbem();
  void CleanupWbem();

  void InitializePerformanceCounters();

  std::string GetCurrentPowerScheme();

  IWbemServices* services_;
  std::unique_ptr<PerformanceQuery> performance_query_;
  std::shared_ptr<PerformanceCounterGroup> cpu_counters_;
  std::shared_ptr<PerformanceCounterArrayGroup> npu_gpu_counters_;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_WINDOWS_H_
