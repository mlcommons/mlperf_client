// system_info_provider.cpp

#include "system_info_provider.h"

#ifdef __APPLE__
#include "system_info_provider_ios.h"
#include "system_info_provider_macos.h"
#include <TargetConditionals.h>
#else
#include "system_info_provider_windows.h"
#endif

#include <mutex>

namespace cil {

SystemInfoProvider* SystemInfoProvider::Instance() {
#ifdef WIN32
  static SystemInfoProvider* provider = new SystemInfoProviderWindows();
#elif !TARGET_OS_IOS
  static SystemInfoProvider* provider = new SystemInfoProviderMacOS();
#else
  static SystemInfoProvider* provider = new SystemInfoProviderIOS();
#endif
  static std::once_flag init_flag;
  std::call_once(init_flag, []() { provider->FetchInfo(); });
  return provider;
}

const SystemInfoProvider::PerformanceInfo&
SystemInfoProvider::GetPerformanceInfo() {
  FetchPerformanceInfo();
  return performance_info_;
}

}  // namespace cil