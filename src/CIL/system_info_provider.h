#ifndef SYSTEM_INFO_PROVIDER_H_
#define SYSTEM_INFO_PROVIDER_H_

#include <string>
#include <vector>

namespace cil {

class SystemInfoProvider {
 public:
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
    size_t memory_size;  // in bytes
    bool operator==(const GPUInfo& other) const {
      return name == other.name && vendor == other.vendor &&
             driver_version == other.driver_version &&
             memory_size == other.memory_size;
    }
  };

  struct MemoryInfo {
    // all in bytes
    size_t physical_available;
    size_t physical_total;
  };

  struct OSInfo {
    std::string name;
    std::string full_name;
    std::string version;  // Major.Minor.Patch build number
  };

  void FetchInfo() {
    FetchCpuInfo();
    FetchGpuInfo();
    FetchSystemMemoryInfo();
    FetchOsInfo();
  }

  const CPUInfo& GetCpuInfo() const { return cpu_info_; }
  const std::vector<GPUInfo>& GetGpuInfo() const { return gpu_info_; }
  const MemoryInfo& GetMemoryInfo() const { return memory_info_; }
  const OSInfo& GetOsInfo() const { return os_info_; }

 protected:
  virtual void FetchCpuInfo() = 0;
  virtual void FetchGpuInfo() = 0;
  virtual void FetchSystemMemoryInfo() = 0;
  virtual void FetchOsInfo() = 0;

  CPUInfo cpu_info_;
  std::vector<GPUInfo> gpu_info_;
  MemoryInfo memory_info_;
  OSInfo os_info_;
};

}  // namespace cil

#endif  // SYSTEM_INFO_PROVIDER_H_
