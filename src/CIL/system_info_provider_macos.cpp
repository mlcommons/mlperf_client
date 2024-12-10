#include "system_info_provider_macos.h"

#ifdef USE_OPENCL
#include <OpenCL/opencl.h>
#endif
#include <log4cxx/logger.h>

#include <infoware/cpu.hpp>
#include <infoware/system.hpp>

using namespace log4cxx;
LoggerPtr loggerSystemInfoProviderMacOS(
    Logger::getLogger("SystemInfoProvider"));

namespace cil {

static std::string cpu_architecture_name(
    iware::cpu::architecture_t architecture) noexcept {
  switch (architecture) {
    case iware::cpu::architecture_t::x64:
      return "x64";
    case iware::cpu::architecture_t::arm:
      return "ARM";
    case iware::cpu::architecture_t::itanium:
      return "Itanium";
    case iware::cpu::architecture_t::x86:
      return "x86";
    default:
      return "Unknown";
  }
}

void SystemInfoProviderMacOS::FetchCpuInfo() {
  try {
    auto quantities = iware::cpu::quantities();
    cpu_info_ = CPUInfo{cpu_architecture_name(iware::cpu::architecture()),
                        iware::cpu::model_name(),
                        iware::cpu::vendor_id(),
                        iware::cpu::frequency(),
                        quantities.logical,
                        quantities.physical};
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch CPU info: " << e.what());
    cpu_info_ = CPUInfo{"", "", "", 0, 0, 0};
  }
}

void SystemInfoProviderMacOS::FetchGpuInfo() {
#ifdef USE_OPENCL
  try {
    cl_platform_id platforms[64];
    cl_uint platforms_used;
    clGetPlatformIDs(sizeof(platforms) / sizeof(*platforms), platforms,
                     &platforms_used);

    for (auto i = 0; i < platforms_used; ++i) {
      cl_device_id devices[64];
      cl_uint devices_used;
      clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU,
                     sizeof(devices) / sizeof(*devices), devices,
                     &devices_used);

      for (auto j = 0; j < devices_used; ++j) {
        char name[256] = {0};
        char vendorname[256] = {0};
        char driver_version[256] = {0};
        cl_ulong memory;

        clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(memory),
                        &memory, nullptr);
        clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR,
                        sizeof(vendorname) / sizeof(*vendorname), &vendorname,
                        nullptr);
        clGetDeviceInfo(devices[j], CL_DEVICE_NAME,
                        sizeof(name) / sizeof(*name), &name, nullptr);
        clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, sizeof(driver_version),
                        driver_version, nullptr);
        GPUInfo info{name, vendorname, driver_version, memory};
        if (std::find(gpu_info_.begin(), gpu_info_.end(), info) ==
            gpu_info_.end())
          gpu_info_.push_back(info);
      }
    }

  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Exception occured when fetching GPU info: " << e.what());
  }
#endif  // USE_OPENCL
  if (gpu_info_.empty())
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch GPU info, no GPU detected");
}

void SystemInfoProviderMacOS::FetchSystemMemoryInfo() {
  try {
    auto memory = iware::system::memory();
    memory_info_ = MemoryInfo{memory.physical_available, memory.physical_total};
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch memory info: " << e.what());
    memory_info_ = MemoryInfo{0, 0};
  }
}

void SystemInfoProviderMacOS::FetchOsInfo() {
  try {
    auto os_info = iware::system::OS_info();
    os_info_ = OSInfo{os_info.name, os_info.full_name,
                      std::to_string(os_info.major) + '.' +
                          std::to_string(os_info.minor) + '.' +
                          std::to_string(os_info.patch) + " build " +
                          std::to_string(os_info.build_number)};
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch OS info: " << e.what());
  }
}

}  // namespace cil