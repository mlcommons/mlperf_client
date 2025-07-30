#import <Metal/Metal.h>
#include <log4cxx/logger.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "system_info_provider_ios.h"

using namespace log4cxx;
LoggerPtr loggerSystemInfoProviderIOS(Logger::getLogger("SystemInfoProvider"));

namespace cil {

void SystemInfoProviderIOS::FetchCpuInfo() {
  std::string architecture;
  unsigned logical_cpus = 0;
  unsigned physical_cpus = 0;
  std::string cpu_model;

  size_t size = 0;

  cpu_type_t cpu_type;
  size = sizeof(cpu_type);
  if (sysctlbyname("hw.cputype", &cpu_type, &size, NULL, 0) == 0) {
    if (cpu_type == CPU_TYPE_ARM64)
      architecture = "arm64";
    else if (cpu_type == CPU_TYPE_ARM)
      architecture = "arm";
  } else {
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve CPU type for architecture detection");
  }

  size = sizeof(logical_cpus);
  if (sysctlbyname("hw.logicalcpu", &logical_cpus, &size, nullptr, 0) != 0)
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve logical CPU count");

  size = sizeof(physical_cpus);
  if (sysctlbyname("hw.physicalcpu", &physical_cpus, &size, nullptr, 0) != 0)
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve physical CPU count");

  char buffer[128];
  size = sizeof(buffer);
  if (sysctlbyname("hw.machine", buffer, &size, nullptr, 0) != 0) {
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve machine name");
  } else {
    std::string machine_name(buffer);

    if (machine_name.find("iPad13", 0) == 0) {
      cpu_model = "Apple M1";
    } else if (machine_name.find("iPad14", 0) == 0) {
      cpu_model = "Apple M2";
    } else if (machine_name.find("iPad16", 0) == 0) {
      cpu_model = "Apple M4";
    } else {
      LOG4CXX_WARN(loggerSystemInfoProviderIOS,
                   "Unknown machine name: " << machine_name);
    }
  }

  cpu_info_ = CPUInfo{
      architecture, cpu_model, "Apple", 0, logical_cpus, physical_cpus,
  };
}

void SystemInfoProviderIOS::FetchGpuInfo() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
      LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                    "Failed to create Metal device");
      gpu_info_ = std::vector<GPUInfo>(1, GPUInfo{"", "Apple", "", 0});
      return;
    }

    std::string name = [device.name UTF8String];
    size_t memory_size = 0;  // device.recommendedMaxWorkingSetSize;

    gpu_info_ =
        std::vector<GPUInfo>(1, GPUInfo{name, "Apple", "", 0, memory_size});
  }
}

void SystemInfoProviderIOS::FetchNpuInfo() {}

void SystemInfoProviderIOS::FetchSystemMemoryInfo() {
  size_t physical_total = 0;
  size_t physical_available = 0;

  /*size_t size = sizeof(physical_total);
  if (sysctlbyname("hw.memsize", &physical_total, &size, nullptr, 0) != 0)
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve total physical memory");

  mach_port_t host_port = mach_host_self();
  vm_statistics64_data_t vm_stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

  if (host_statistics64(host_port, HOST_VM_INFO64,
                        reinterpret_cast<host_info64_t>(&vm_stats),
                        &count) != KERN_SUCCESS) {
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve available physical memory");
  } else {
    physical_available =
        vm_stats.free_count * static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
  }*/

  memory_info_ = MemoryInfo{
      physical_available,
      physical_total,
  };
}

void SystemInfoProviderIOS::FetchOsInfo() {
  std::string os_release;
  std::string os_type;
  std::string os_version;

  char buffer[256];
  size_t size = sizeof(buffer);

  if (sysctlbyname("kern.ostype", buffer, &size, nullptr, 0) == 0) {
    os_type = std::string(buffer);
  } else {
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve OS type (kern.ostype)");
  }

  size = sizeof(buffer);
  if (sysctlbyname("kern.osrelease", buffer, &size, nullptr, 0) == 0) {
    os_release = std::string(buffer);
  } else {
    LOG4CXX_ERROR(loggerSystemInfoProviderIOS,
                  "Failed to retrieve OS release (kern.osrelease)");
  }

  os_info_ = OSInfo{os_type, os_type, os_release};
}

void SystemInfoProviderIOS::FetchPerformanceInfo() {}

}  // namespace cil