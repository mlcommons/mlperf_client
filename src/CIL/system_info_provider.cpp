// system_info_provider.cpp

#include "system_info_provider.h"

#ifdef __APPLE__
#include "system_info_provider_ios.h"
#include "system_info_provider_macos.h"
#include <TargetConditionals.h>
#elif defined(__linux__)
#include "system_info_provider_linux.h"
#else
#include "system_info_provider_windows.h"
#endif

#include <mutex>

namespace cil {

auto SystemInfoProvider::Instance() -> SystemInfoProvider* {
#ifdef WIN32
  static SystemInfoProvider* provider = new SystemInfoProviderWindows();
#elif !TARGET_OS_IOS && defined(__APPLE__)
  static SystemInfoProvider* provider = new SystemInfoProviderMacOS();
#elif defined(__linux__)
  static SystemInfoProvider* provider = new SystemInfoProviderLinux();
#else
  static SystemInfoProvider* provider = new SystemInfoProviderIOS();
#endif
  static std::once_flag init_flag;
  std::call_once(init_flag, []() { provider->FetchInfo(); });
  return provider;
}

auto SystemInfoProvider::GetPerformanceInfo() -> const SystemInfoProvider::PerformanceInfo& {
  FetchPerformanceInfo();
  return performance_info_;
}

}  // namespace cil