#ifndef SYSTEM_INFO_PROVIDER_WINDOWS_H_
#define SYSTEM_INFO_PROVIDER_WINDOWS_H_

#include "system_info_provider.h"

struct IWbemLocator;
struct IWbemServices;

namespace cil {

class SystemInfoProviderWindows : public SystemInfoProvider {
 public:
  SystemInfoProviderWindows();
  ~SystemInfoProviderWindows() override;

 protected:
  void FetchCpuInfo() override;
  void FetchGpuInfo() override;
  void FetchSystemMemoryInfo() override;
  void FetchOsInfo() override;

 private:
  void Initialize();
  void Cleanup();

  IWbemServices* services_;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_WINDOWS_H_
