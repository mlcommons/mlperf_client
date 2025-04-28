#ifndef ADAPTER_ENUMERATION_H
#define ADAPTER_ENUMERATION_H

#include <string>
#include <vector>

namespace cil {
namespace common {
namespace DirectML {
#ifdef WIN32

struct AdapterInfo {
  std::string name;
  std::string type;
  unsigned long luid_low_part;
  long luid_high_part;
};

std::vector<AdapterInfo> EnumerateAdapters(const std::string& dlls_directory);

class DeviceEnumeration {
 public:
  DeviceEnumeration() = default;
  ~DeviceEnumeration() = default;

  typedef AdapterInfo DeviceInfo;
  typedef std::vector<DeviceInfo> DeviceList;

  const DeviceList& EnumerateDevices(const std::string& dlls_directory,
                                     const std::string& device_type,
                                     const std::string& device_vendor);

  const DeviceList& GetDevices() const { return devices_; }

 protected:
  DeviceList devices_;
};

#endif
}  // namespace DirectML
}  // namespace common
}  // namespace cil

#endif