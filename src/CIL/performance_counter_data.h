#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>

namespace cil {

class PerformanceCounterDataBase {
 public:
  virtual ~PerformanceCounterDataBase() = default;

  virtual void Prepare() = 0;
  virtual void Process(int index, const wchar_t* name, const wchar_t* subname,
                       double value) = 0;

  virtual size_t GetDeviceCount() const = 0;
  virtual const std::wstring& GetDeviceName(int device_index) const = 0;
  virtual const std::string& GetDeviceType(int device_index) const = 0;
  virtual const std::map<int, double>& GetValues(int device_index) const = 0;
};

class PerformanceCounterData : public PerformanceCounterDataBase {
 public:
  virtual void Prepare() { values_.clear(); }
  virtual void Process(int index, const wchar_t* name, const wchar_t* subname,
                       double value) {
    values_[index] += value;
  }

  virtual size_t GetDeviceCount() const { return 1; }
  virtual std::wstring& GetDeviceName(int device_index) const {
    static std::wstring kBlank;
    return kBlank;
  }
  virtual const std::string& GetDeviceType(int device_index) const {
    static std::string kBlank;
    return kBlank;
  }
  virtual const std::map<int, double>& GetValues(int device_index) const {
    return values_;
  }

 private:
  std::map<int, double> values_;
};

class PerformanceCounterDataNpuGpu : public PerformanceCounterDataBase {
 public:
  static const std::string kGPU;
  static const std::string kNPU;

  struct Memory  // in bytes
  {
    size_t dedicated_video_memory = 0;
    size_t dedicated_system_memory = 0;
    size_t shared_system_memory = 0;  // or Shared GPU/NPU Memory
  };

  //
  PerformanceCounterDataNpuGpu();

  virtual void Prepare();
  virtual void Process(int index, const wchar_t* name, const wchar_t* subname,
                       double value);

  virtual size_t GetDeviceCount() const { return devices_.size(); }
  virtual const std::wstring& GetDeviceName(int device_index) const {
    return devices_[device_index].name;
  }
  virtual const std::string& GetDeviceType(int device_index) const {
    return (devices_[device_index].type == eGPU) ? kGPU : kNPU;
  }
  virtual const std::map<int, double>& GetValues(int device_index) const {
    return devices_[device_index].values;
  }

  const Memory& GetDeviceMemory(int device_index) const {
    return devices_[device_index].memory;
  }

 private:
  enum DeviceType { eGPU, eNPU };

  struct Device {
    DeviceType type;
    std::wstring name;
    LUID luid;
    uint32_t phy_id;

    Memory memory;

    //
    std::wstring counter_id;
    std::map<int, double> values;
  };

  std::vector<Device> FindDevices(DeviceType type);
  void UpdateDeviceMemory();

  //
  std::vector<Device> devices_;
};

}  // namespace cil