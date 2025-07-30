#ifndef SYSTEM_INFO_PROVIDER_IOS_H_
#define SYSTEM_INFO_PROVIDER_IOS_H_

#include "system_info_provider.h"

namespace cil {

class SystemInfoProviderIOS : public SystemInfoProvider {
 protected:
  void FetchCpuInfo() override;
  void FetchGpuInfo() override;
  void FetchNpuInfo() override;
  void FetchSystemMemoryInfo() override;
  void FetchOsInfo() override;

  void FetchPerformanceInfo() override;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_IOS_H_
