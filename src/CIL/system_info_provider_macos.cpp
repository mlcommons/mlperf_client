#include "system_info_provider_macos.h"

#include "io_report_reader.h"
#include "smc_reader.h"

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

SystemInfoProviderMacOS::SystemInfoProviderMacOS()
    : smc_reader_(nullptr), io_report_reader_(nullptr) {
  performance_info_.cpu_usage = -1;
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
  if (cpu_info_.architecture == "ARM") {
    npu_info_.name = "ANE";
    npu_info_.dedicated_memory_size = npu_info_.shared_memory_size = 0;
    npu_info_.vendor = "Apple";
  }
}

void SystemInfoProviderMacOS::FetchSystemMemoryInfo() {
  try {
    auto memory = iware::system::memory();
    memory_info_ =
        MemoryInfo{memory.physical_available, memory.physical_total, 0};
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch memory info: " << e.what());
    memory_info_ = MemoryInfo{0, 0, 0};
  }
}

void SystemInfoProviderMacOS::FetchOsInfo() {
  try {
    auto os_info = iware::system::OS_info();
    os_info_ = OSInfo{os_info.name, os_info.name,
                      std::to_string(os_info.major) + '.' +
                          std::to_string(os_info.minor) + '.' +
                          std::to_string(os_info.patch) + " build " +
                          std::to_string(os_info.build_number)};
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch OS info: " << e.what());
  }
}

void SystemInfoProviderMacOS::FetchPerformanceInfo() {
  FetchCpuUsage();
  FetchMemoryUsage();

  if (performance_info_.gpu_info.empty()) {
    std::string gpu_name = gpu_info_.front().name;
    performance_info_.gpu_info[gpu_name] =
        NPUGPUPerformanceInfo{gpu_name, 0, 0.0, 0, 0};
  }
  if (performance_info_.npu_info.name.empty())
    performance_info_.npu_info =
        NPUGPUPerformanceInfo{npu_info_.name, 0, 0.0, 0, 0};

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
  vm_statistics64_data_t vmStats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

  int ret = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info_t)&vmStats, &count);
  if (ret != KERN_SUCCESS) {
    LOG4CXX_ERROR(loggerSystemInfoProviderMacOS,
                  "Failed to fetch memory statistics, ret: " << ret);
    return;
  }

  mach_vm_size_t pageSize = vm_kernel_page_size;
  performance_info_.memory_usage =
      (size_t)(vmStats.wire_count + vmStats.compressor_page_count +
               vmStats.internal_page_count) *
      pageSize;
}

void SystemInfoProviderMacOS::FetchNpuGpuUsages() {
  if (!io_report_reader_)
    io_report_reader_ = std::make_shared<IOReportReader>();

  auto gpu_npu_usage = io_report_reader_->GetGpuAndNpuUsage();
  performance_info_.gpu_info.begin()->second.usage = gpu_npu_usage.first;
  performance_info_.npu_info.usage = gpu_npu_usage.second;
}

void SystemInfoProviderMacOS::FetchCpuGpuTemperatures() {
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
    if (value == 0) continue;
    if (key.rfind("Tp", 0) == 0) {
      cpu_temp_sum += value;
      cpu_sensor_count++;
    } else if (key.rfind("Tg", 0) == 0) {
      gpu_temp_sum += value;
      gpu_sensor_count++;
    }
  }

  performance_info_.cpu_temperature =
      cpu_sensor_count > 0 ? cpu_temp_sum / cpu_sensor_count : 50.0;
  performance_info_.cpu_temperature_is_available = true;

  auto& gpu_info = performance_info_.gpu_info.begin()->second;
  gpu_info.temperature =
      gpu_sensor_count > 0 ? gpu_temp_sum / gpu_sensor_count : 0;
  gpu_info.temperature_is_available = true;
}
}  // namespace cil
