#ifndef SYSTEM_INFO_PROVIDER_MACOS_H_
#define SYSTEM_INFO_PROVIDER_MACOS_H_

#include "system_info_provider.h"

namespace cil {

class SystemInfoProviderMacOS : public SystemInfoProvider {
 protected:
  void FetchCpuInfo() override;
  void FetchGpuInfo() override;
  void FetchSystemMemoryInfo() override;
  void FetchOsInfo() override;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_MACOS_H_
