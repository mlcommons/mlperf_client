#include "system_info_provider_macos.h"

#if !MLPERF_PUBLISHING
#include "io_report_reader.h"
#include "smc_reader.h"
#endif

#ifdef USE_OPENCL
#include <OpenCL/opencl.h>
#endif
#include <log4cxx/logger.h>
#include <sys/sysctl.h>

using namespace log4cxx;
LoggerPtr loggerSystemInfoProviderMacOS(
    Logger::getLogger("SystemInfoProvider"));

namespace cil {

static std::string SysCtlGetString(const std::string& name) {
  size_t size = 0;
  if (sysctlbyname(name.c_str(), nullptr, &size, nullptr, 0) != 0 ||
      size == 0) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to retrieve size of (" + name + ")");
    return {};
  }

  std::string result(size, '\0');
  if (sysctlbyname(name.c_str(), result.data(), &size, nullptr, 0) != 0) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to retrieve (" + name + ")");
    return {};
  }

  if (!result.empty() && result.back() == '\0') result.pop_back();

  return result;
}

template <typename NumberType>
static NumberType SysCtlGetNumber(const std::string& name) {
  NumberType value = {};
  size_t size = sizeof(value);
  if (sysctlbyname(name.c_str(), &value, &size, nullptr, 0) != 0) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to retrieve (" + name + ")");
    return {};
  }
  return value;
}

SystemInfoProviderMacOS::SystemInfoProviderMacOS()
#if !MLPERF_PUBLISHING
    : smc_reader_(nullptr),
      io_report_reader_(nullptr)
#endif
{
  performance_info_.cpu_usage = -1;
}

void SystemInfoProviderMacOS::FetchCpuInfo() {
  std::string architecture;
  cpu_type_t cpu_type = SysCtlGetNumber<cpu_type_t>("hw.cputype");
  if (cpu_type == CPU_TYPE_ARM64)
    architecture = "arm64";
  else if (cpu_type == CPU_TYPE_ARM)
    architecture = "arm";

  unsigned logical_cpus = SysCtlGetNumber<unsigned>("hw.logicalcpu");
  unsigned physical_cpus = SysCtlGetNumber<unsigned>("hw.physicalcpu");
  std::string cpu_model = SysCtlGetString("machdep.cpu.brand_string");

  cpu_info_ = CPUInfo{
      architecture, cpu_model, "Apple", 0, logical_cpus, physical_cpus,
  };
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
        cl_ulong global_memory = 0;

        // clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_SIZE,
        //                 sizeof(global_memory), &global_memory, nullptr);
        clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR,
                        sizeof(vendorname) / sizeof(*vendorname), &vendorname,
                        nullptr);
        clGetDeviceInfo(devices[j], CL_DEVICE_NAME,
                        sizeof(name) / sizeof(*name), &name, nullptr);
        clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, sizeof(driver_version),
                        driver_version, nullptr);
        GPUInfo info{name, vendorname, driver_version, 0, global_memory};
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

void SystemInfoProviderMacOS::FetchNpuInfo() {
  if (cpu_info_.architecture == "arm64") {
    npu_info_.name = "ANE";
    npu_info_.dedicated_memory_size = npu_info_.shared_memory_size = 0;
    npu_info_.vendor = "Apple";
  }
}

void SystemInfoProviderMacOS::FetchSystemMemoryInfo() {
  size_t physical_total = SysCtlGetNumber<size_t>("hw.memsize");
  size_t physical_available = physical_total - getMemoryUsage();
  memory_info_ = MemoryInfo{physical_available, physical_total, 0};
}

void SystemInfoProviderMacOS::FetchOsInfo() {
  std::string os_type = SysCtlGetString("kern.ostype");
  std::string os_release = SysCtlGetString("kern.osrelease");
  size_t build_number = SysCtlGetNumber<size_t>("kern.osrevision");

  os_info_ = OSInfo{os_type, os_type,
                    os_release + " build " + std::to_string(build_number)};
}

void SystemInfoProviderMacOS::FetchPerformanceInfo() {
  FetchCpuUsage();
  FetchMemoryUsage();

  if (performance_info_.gpu_info.empty()) {
    std::string gpu_name = gpu_info_.front().name;
    performance_info_.gpu_info[gpu_name] =
        NPUGPUPerformanceInfo{gpu_name, 0, false, 0.0, false, 0, 0, false};
  }
  if (performance_info_.npu_info.name.empty())
    performance_info_.npu_info = NPUGPUPerformanceInfo{
        npu_info_.name, 0, false, 0.0, false, 0, 0, false};

  FetchNpuGpuUsages();
  FetchCpuGpuTemperatures();

  performance_info_.power_scheme_is_available = false;
}

void SystemInfoProviderMacOS::FetchCpuUsage() {
  host_cpu_load_info_data_t current_info;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;

  int ret = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                            (host_info_t)&current_info, &count);
  if (ret != KERN_SUCCESS) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch CPU statistics, ret: " << ret);
    return;
  }

  if (performance_info_.cpu_usage == -1) {
    cpu_prev_load_info_ = current_info;
    performance_info_.cpu_usage = 0;
    return;
  }

  uint32_t diffs[CPU_STATE_MAX] = {0};
  uint32_t total = 0;
  for (int i = 0; i < CPU_STATE_MAX; ++i) {
    diffs[i] = current_info.cpu_ticks[i] - cpu_prev_load_info_.cpu_ticks[i];
    total += diffs[i];
  }
  if (total == 0) total = 1;  // avoid division by zero
  performance_info_.cpu_usage =
      (diffs[CPU_STATE_USER] + diffs[CPU_STATE_SYSTEM]) * 100.0 / total;

  cpu_prev_load_info_ = current_info;
}

void SystemInfoProviderMacOS::FetchMemoryUsage() {
  performance_info_.memory_usage = getMemoryUsage();
}

size_t SystemInfoProviderMacOS::getMemoryUsage() const {
  vm_statistics64_data_t vmStats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

  int ret = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info_t)&vmStats, &count);
  if (ret != KERN_SUCCESS) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch memory statistics, ret: " << ret);
    return 0;
  }

  mach_vm_size_t pageSize = vm_kernel_page_size;
  return (size_t)(vmStats.wire_count + vmStats.compressor_page_count +
                  vmStats.internal_page_count) *
         pageSize;
}

void SystemInfoProviderMacOS::FetchNpuGpuUsages() {
#if !MLPERF_PUBLISHING
  if (!io_report_reader_)
    io_report_reader_ = std::make_shared<IOReportReader>();

  auto gpu_npu_usage = io_report_reader_->GetGpuAndNpuUsage();
  performance_info_.gpu_info.begin()->second.usage = gpu_npu_usage.first;
  performance_info_.gpu_info.begin()->second.usage_is_available = true;
  performance_info_.npu_info.usage = gpu_npu_usage.second;
  performance_info_.npu_info.usage_is_available = true;
#endif
}

void SystemInfoProviderMacOS::FetchCpuGpuTemperatures() {
#if !MLPERF_PUBLISHING
  if (!smc_reader_) {
    smc_reader_ = std::make_shared<SMCReader>();
    smc_reader_->Open();
  }
  auto temps = smc_reader_->GetThermalSensorValues();

  float cpu_temp_sum = 0.0f;
  int cpu_sensor_count = 0;

  float gpu_temp_sum = 0.0f;
  int gpu_sensor_count = 0;

  for (const auto& [key, value] : temps) {
    if (value < 10.0f) continue;
    if (key.rfind("Tp", 0) == 0) {
      cpu_temp_sum += value;
      cpu_sensor_count++;
    } else if (key.rfind("Tg", 0) == 0) {
      gpu_temp_sum += value;
      gpu_sensor_count++;
    }
  }

  if (cpu_sensor_count > 0)
    performance_info_.cpu_temperature = cpu_temp_sum / cpu_sensor_count;
  performance_info_.cpu_temperature_is_available = true;

  auto& gpu_info = performance_info_.gpu_info.begin()->second;
  if (gpu_sensor_count > 0)
    gpu_info.temperature = gpu_temp_sum / gpu_sensor_count;
  gpu_info.temperature_is_available = true;
#endif
}
}  // namespace cil
