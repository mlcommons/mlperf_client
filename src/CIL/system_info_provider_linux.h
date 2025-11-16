#ifndef SYSTEM_INFO_PROVIDER_LINUX_H_
#define SYSTEM_INFO_PROVIDER_LINUX_H_

#include "system_info_provider.h"

namespace cil {

class SystemInfoProviderLinux : public SystemInfoProvider {
 public:
  SystemInfoProviderLinux() = default;
  ~SystemInfoProviderLinux() = default;

 protected:
  void FetchCpuInfo() override;
  void FetchGpuInfo() override;
  void FetchNpuInfo() override;
  void FetchSystemMemoryInfo() override;
  void FetchOsInfo() override;
  void FetchPerformanceInfo() override;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_LINUX_H_
