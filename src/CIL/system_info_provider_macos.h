#ifndef SYSTEM_INFO_PROVIDER_MACOS_H_
#define SYSTEM_INFO_PROVIDER_MACOS_H_

#include <mach/mach.h>

#include "system_info_provider.h"

class SMCReader;
class IOReportReader;

namespace cil {

class SystemInfoProviderMacOS : public SystemInfoProvider {
 public:
  SystemInfoProviderMacOS();

 protected:
  void FetchCpuInfo() override;
  void FetchGpuInfo() override;
  void FetchNpuInfo() override;
  void FetchSystemMemoryInfo() override;
  void FetchOsInfo() override;

  void FetchPerformanceInfo() override;
  void FetchCpuUsage();
  void FetchMemoryUsage();
  void FetchNpuGpuUsages();
  void FetchCpuGpuTemperatures();

 private:
  host_cpu_load_info_data_t cpu_prev_load_info_;
  std::shared_ptr<SMCReader> smc_reader_;
  std::shared_ptr<IOReportReader> io_report_reader_;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_MACOS_H_
