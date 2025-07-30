#ifndef SYSTEM_INFO_PROVIDER_H_
#define SYSTEM_INFO_PROVIDER_H_

#include <map>
#include <string>
#include <vector>

namespace cil {

class SystemInfoProvider {
 public:
  static SystemInfoProvider* Instance();

  virtual ~SystemInfoProvider() = default;
  struct CPUInfo {
    std::string architecture;
    std::string model_name;
    std::string vendor_id;
    unsigned long long frequency;  // in Hz
    unsigned int logical_cpus;
    unsigned int physical_cpus;
  };

  struct GPUInfo {
    std::string name;
    std::string vendor;
    std::string driver_version;
    size_t dedicated_memory_size;  // in bytes
    size_t shared_memory_size;     // in bytes
    bool operator==(const GPUInfo& other) const {
      return name == other.name && vendor == other.vendor &&
             driver_version == other.driver_version &&
             dedicated_memory_size == other.dedicated_memory_size &&
             shared_memory_size == other.shared_memory_size;
    }
  };

  struct NPUInfo {
    std::string name;
    std::string vendor;
    size_t dedicated_memory_size;  // in bytes
    size_t shared_memory_size;     // in bytes
  };

  struct MemoryInfo {
    // all in bytes
    size_t physical_available;
    size_t physical_total;
    size_t virtual_total;
  };

  struct OSInfo {
    std::string name;
    std::string full_name;
    std::string version;  // Major.Minor.Patch build number
  };

  struct NPUGPUPerformanceInfo {
    std::string name;
    size_t usage;
    double temperature;
    bool temperature_is_available;
    size_t dedicated_memory_usage;
    size_t shared_memory_usage;
  };

  struct PerformanceInfo {
    size_t cpu_usage;
    double cpu_temperature;
    bool cpu_temperature_is_available;
    size_t memory_usage;
    std::string power_scheme;
    bool power_scheme_is_available;
    std::map<std::string, NPUGPUPerformanceInfo> gpu_info;
    NPUGPUPerformanceInfo npu_info;
  };

  virtual void FetchInfo() {
    FetchCpuInfo();
    FetchGpuInfo();
    FetchNpuInfo();
    FetchSystemMemoryInfo();
    FetchOsInfo();
  }

  const CPUInfo& GetCpuInfo() const { return cpu_info_; }
  const std::vector<GPUInfo>& GetGpuInfo() const { return gpu_info_; }
  const NPUInfo& GetNpuInfo() const { return npu_info_; }
  const MemoryInfo& GetMemoryInfo() const { return memory_info_; }
  const OSInfo& GetOsInfo() const { return os_info_; }

  const PerformanceInfo& GetPerformanceInfo();

 protected:
  virtual void FetchCpuInfo() = 0;
  virtual void FetchGpuInfo() = 0;
  virtual void FetchNpuInfo() = 0;
  virtual void FetchSystemMemoryInfo() = 0;
  virtual void FetchOsInfo() = 0;

  virtual void FetchPerformanceInfo() = 0;

  CPUInfo cpu_info_;
  std::vector<GPUInfo> gpu_info_;
  NPUInfo npu_info_;
  MemoryInfo memory_info_;
  OSInfo os_info_;

  PerformanceInfo performance_info_;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_H_
