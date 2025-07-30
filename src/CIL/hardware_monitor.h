#ifndef HARDWARE_MONITOR_H_
#define HARDWARE_MONITOR_H_

#include <cstdint>
#include <string>
#include <vector>

struct CpuUsage {
  double total_usage;
  std::vector<double> core_usages;
  uint64_t clock_speed;
};

struct MemoryUsage {
  uint64_t total_memory;
  uint64_t used_memory;
  uint64_t free_memory;
};

struct GpuUsage {
  std::string name;
  std::string driver_version;
  double utilization_gpu;
  double utilization_memory;
  uint64_t total_memory;
  uint64_t used_memory;
  uint64_t free_memory;
  double temperature;
  double fan_speed;
  double power_draw;
  uint64_t clock_speed_gpu;
  uint64_t clock_speed_memory;
};

struct ProcessGpuUsage {
  std::string name;
  double utilization_gpu;
};

struct FullGpuUsage {
  std::vector<GpuUsage> native_gpu_usage;
  std::vector<ProcessGpuUsage> third_party_gpu_usage;
};

struct NpuUsage {
  double utilization;
  uint64_t clock_speed;
};

struct DiskUsage {
  uint64_t total_space;
  uint64_t used_space;
  uint64_t free_space;
  double read_rate;
  double write_rate;
};

struct Temperature {
  double cpu;
  std::vector<double> gpus;
  double system_ambient;
};

struct SystemInfo {
  std::string os_name;
  std::string os_version;
  std::string cpu_model;
  uint32_t num_cpu_cores;
  uint32_t num_cpu_threads;
  std::vector<std::string> gpu_models;
};

class HardwareMonitor {
 public:
  virtual ~HardwareMonitor() = default;

  virtual CpuUsage GetTotalCpuUsage() const = 0;
  virtual double GetCpuUsageByPid(uint64_t pid) const = 0;

  virtual MemoryUsage GetTotalMemoryUsage() const = 0;
  virtual double GetMemoryUsageByPid(uint64_t pid) const = 0;

  virtual FullGpuUsage GetFullGpuUsage() const = 0;
  virtual std::vector<ProcessGpuUsage> GetGpuUsageByPid(uint64_t pid) const = 0;

  virtual NpuUsage GetTotalNpuUsage() const = 0;
  virtual double GetNpuUsageByPid(uint64_t pid) const = 0;

  virtual DiskUsage GetTotalDiskUsage() const = 0;

  virtual Temperature GetTemperature() const = 0;

  virtual SystemInfo GetSystemInfo() const = 0;

  virtual std::string GetCurrentPowerScheme() const = 0;
};

#endif  // HARDWARE_MONITOR_H_