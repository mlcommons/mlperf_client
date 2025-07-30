#include "performance_counter_data.h"
#define INITGUID
#include <devguid.h>  // Include for GUID_DEVCLASS_DISPLAY, etc.
#include <devpkey.h>
#include <initguid.h>
#include <log4cxx/logger.h>
#include <setupapi.h>
#include <tchar.h>

#include <iomanip>
#include <iostream>
#include <sstream>

#pragma comment(lib, "setupapi.lib")

using namespace log4cxx;
extern LoggerPtr loggerSystemInfoProviderWindows;

namespace {
std::string GetLastErrorAsString() {
  DWORD errorMessageID = ::GetLastError();
  if (errorMessageID == 0) return std::string();

  LPSTR messageBuffer = nullptr;
  size_t size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&messageBuffer, 0, NULL);

  std::string message(messageBuffer, size);

  LocalFree(messageBuffer);
  return message;
}

bool GetDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData,
                       DEVPROPKEY propertyKey, DWORD& propertyType,
                       std::vector<BYTE>& propertyBuffer) {
  DWORD requiredSize;

  // First call to get required buffer size
  if (!SetupDiGetDevicePropertyW(hDevInfo, &devInfoData, &propertyKey,
                                 &propertyType, nullptr, 0, &requiredSize, 0)) {
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) {
      LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                    "SetupDiGetDevicePropertyW (size) failed: "
                        << GetLastErrorAsString());
      return false;
    }
  }

  if (requiredSize > 0) {
    propertyBuffer.resize(requiredSize);

    if (!SetupDiGetDevicePropertyW(hDevInfo, &devInfoData, &propertyKey,
                                   &propertyType, propertyBuffer.data(),
                                   requiredSize, nullptr, 0)) {
      LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                    "SetupDiGetDevicePropertyW (data) failed: "
                        << GetLastErrorAsString());
      return false;
    }
  } else {
    propertyBuffer.clear();
  }

  return true;
}

std::wstring GetDevicePropertyString(HDEVINFO hDevInfo,
                                     SP_DEVINFO_DATA& devInfoData,
                                     DEVPROPKEY propertyKey,
                                     bool* ok = nullptr) {
  if (ok) *ok = false;
  DWORD propertyType;
  std::vector<BYTE> propertyBuffer;
  if (GetDeviceProperty(hDevInfo, devInfoData, propertyKey, propertyType,
                        propertyBuffer)) {
    if (propertyType == DEVPROP_TYPE_STRING) {
      if (ok) *ok = true;
      return reinterpret_cast<wchar_t*>(propertyBuffer.data());
    }
  }

  return {};
}

template <typename ResultType, typename Type>
ResultType ÑonvertType(BYTE* data, bool* ok = nullptr) {
  if (ok) *ok = true;
  if (sizeof(ResultType) == sizeof(Type)) {
    return *reinterpret_cast<ResultType*>(data);
  } else if constexpr (std::is_constructible<ResultType, Type>::value) {
    return ResultType(*reinterpret_cast<Type*>(data));
  } else {
    if (ok) *ok = false;
    return ResultType();
  }
}

template <typename ResultType>
ResultType GetDevicePropertyPOD(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData,
                                DEVPROPKEY propertyKey, bool* ok = nullptr) {
  DWORD propertyType;
  std::vector<BYTE> propertyBuffer;
  if (GetDeviceProperty(hDevInfo, devInfoData, propertyKey, propertyType,
                        propertyBuffer)) {
    switch (propertyType) {
      case DEVPROP_TYPE_UINT32:
        return ÑonvertType<ResultType, uint32_t>(propertyBuffer.data(), ok);
      case DEVPROP_TYPE_UINT64:
        return ÑonvertType<ResultType, uint64_t>(propertyBuffer.data(), ok);
      case DEVPROP_TYPE_BOOLEAN:
        return ÑonvertType<ResultType, char>(propertyBuffer.data(), ok);
      case DEVPROP_TYPE_GUID:
        return ÑonvertType<ResultType, GUID>(propertyBuffer.data(), ok);
      default:
        break;
    }
  }

  if (ok) *ok = false;
  return ResultType();
}
}  // namespace

namespace cil {

const std::string PerformanceCounterDataNpuGpu::kGPU = std::string("GPU");
const std::string PerformanceCounterDataNpuGpu::kNPU = std::string("NPU");

/*void PerformanceCounterDataNpuGpu::Print() {
    for (auto& device : devices_) {
        std::wcout << (device.type == eGPU ? L"GPU (" : L"NPU (") << device.name
<< L"): " << std::endl; for (auto& it : device.values) std::cout << "- " <<
counters_[it.first].label << ": " << std::fixed << std::setprecision(2) <<
it.second << " " << counters_[it.first].units << std::endl;
    }
}*/

PerformanceCounterDataNpuGpu::PerformanceCounterDataNpuGpu() {
  auto npu_devices = FindDevices(eNPU);
  auto gpu_devices = FindDevices(eGPU);
  devices_ = npu_devices;
  devices_.insert(devices_.end(), gpu_devices.begin(), gpu_devices.end());

  UpdateDeviceMemory();
}

void PerformanceCounterDataNpuGpu::Prepare() {
  for (auto& device : devices_) {
    device.values.clear();
  }
}

void PerformanceCounterDataNpuGpu::Process(int index, const wchar_t* name,
                                           const wchar_t* subname,
                                           double value) {
  for (auto& device : devices_) {
    if (::wcsstr(name, device.counter_id.data()) != NULL) {
      if (!subname || ::wcsstr(name, subname) != NULL) {
        device.values[index] += value;
      }
    }
  }

  // std::wcout << "Instance: " << name << ", " << /*counter.label.c_str() <<*/
  // ": " << value << " " << /*counter.units.c_str() <<*/ std::endl;
}

std::vector<PerformanceCounterDataNpuGpu::Device>
PerformanceCounterDataNpuGpu::FindDevices(DeviceType type) {
  GUID deviceGuid =
      (type == eNPU) ? GUID_DEVCLASS_COMPUTEACCELERATOR : GUID_DEVCLASS_DISPLAY;
  HDEVINFO deviceInfo = SetupDiGetClassDevs(&deviceGuid, NULL, NULL,
                                            (DIGCF_PROFILE | DIGCF_PRESENT));

  if (deviceInfo == INVALID_HANDLE_VALUE) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "SetupDiGetClassDevs failed. Error: " << GetLastError());
    return {};
  }

  // Enumerate devices in the set
  SP_DEVINFO_DATA deviceInfoData;
  deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  std::vector<Device> devices;
  DWORD deviceIndex = 0;
  while (SetupDiEnumDeviceInfo(deviceInfo, deviceIndex, &deviceInfoData)) {
    //
    const DEVPROPKEY DEVPKEY_Gpu_PhyId = {
        {0x60b193cb,
         0x5276,
         0x4d0f,
         {0x96, 0xfc, 0xf1, 0x73, 0xab, 0xad, 0x3e, 0xc6}},
        3};
    const DEVPROPKEY DEVPKEY_GPU_LUID = {
        {0x60b193cb,
         0x5276,
         0x4d0f,
         {0x96, 0xfc, 0xf1, 0x73, 0xab, 0xad, 0x3e, 0xc6}},
        2};

    Device device;
    device.type = type;
    device.name =
        GetDevicePropertyString(deviceInfo, deviceInfoData, DEVPKEY_NAME);
    device.phy_id = GetDevicePropertyPOD<uint32_t>(deviceInfo, deviceInfoData,
                                                   DEVPKEY_Gpu_PhyId);
    device.luid = GetDevicePropertyPOD<LUID>(deviceInfo, deviceInfoData,
                                             DEVPKEY_GPU_LUID);

    std::wstringstream wss;
    wss << L"0x" << std::hex << std::uppercase << std::setw(8)
        << std::setfill(L'0') << device.luid.HighPart << L"_0x" << std::hex
        << std::uppercase << std::setw(8) << std::setfill(L'0')
        << device.luid.LowPart;

    // std::wcout << "Name: " << device.name << std::endl;
    // std::wcout << "  PhyId: " << device.phy_id << std::endl;
    // std::wcout << "  LUID: " << wss.str() << std::endl;

    wss << L"_phys_" << device.phy_id;
    device.counter_id = L"luid_" + wss.str();
    // std::wcout << "  CounterId: " << device.counter_id << std::endl;

    devices.push_back(device);
    deviceIndex++;
  }

  if (GetLastError() != ERROR_NO_MORE_ITEMS) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "SetupDiEnumDeviceInfo failed. Error: " << GetLastError());
    SetupDiDestroyDeviceInfoList(deviceInfo);
    return {};
  }

  // Clean up
  SetupDiDestroyDeviceInfoList(deviceInfo);
  return devices;
}

}  // namespace cil

//
#include <cfgmgr32.h>
#include <d3dkmthk.h>  // Include the D3DKMT header

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace {
std::wstring GetDeviceInterfacePropertyString(const LPCWSTR deviceInterface,
                                              const DEVPROPKEY& propertyKey) {
  DEVPROPTYPE propertyType;
  ULONG bufferSize = 0;
  std::wstring result;

  CONFIGRET cr = CM_Get_Device_Interface_PropertyW(
      deviceInterface, &propertyKey, &propertyType, nullptr, &bufferSize, 0);

  if (cr == CR_BUFFER_SMALL) {
    std::vector<BYTE> buffer(bufferSize);
    cr = CM_Get_Device_Interface_PropertyW(deviceInterface, &propertyKey,
                                           &propertyType, buffer.data(),
                                           &bufferSize, 0);

    if (cr == CR_SUCCESS && propertyType == DEVPROP_TYPE_STRING) {
      result = reinterpret_cast<wchar_t*>(buffer.data());
    }
  }
  return result;
}

BOOL GetDeviceInstance(const LPCWSTR deviceInterface,
                       DEVINST& deviceInstanceHandle) {
  std::wstring deviceInstanceId = GetDeviceInterfacePropertyString(
      deviceInterface, DEVPKEY_Device_InstanceId);
  if (deviceInstanceId.empty()) return FALSE;

  if (CM_Locate_DevNodeW(&deviceInstanceHandle, deviceInstanceId.data(),
                         CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
    return FALSE;
  }

  return TRUE;
}

std::wstring GetDevNodePropertyString(DEVINST devInst, DEVPROPKEY propertyKey) {
  DEVPROPTYPE propertyType;
  ULONG bufferSize = 0;
  std::wstring result;

  CONFIGRET cr = CM_Get_DevNode_PropertyW(devInst, &propertyKey, &propertyType,
                                          nullptr, &bufferSize, 0);

  if (cr == CR_BUFFER_SMALL) {
    std::vector<BYTE> buffer(bufferSize);
    cr = CM_Get_DevNode_PropertyW(devInst, &propertyKey, &propertyType,
                                  buffer.data(), &bufferSize, 0);

    if (cr == CR_SUCCESS && propertyType == DEVPROP_TYPE_STRING) {
      result = reinterpret_cast<wchar_t*>(buffer.data());
    }
  }
  return result;
}

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

NTSTATUS QueryAdapterInformation(_In_ D3DKMT_HANDLE AdapterHandle,
                                 _In_ KMTQUERYADAPTERINFOTYPE InformationClass,
                                 _Out_writes_bytes_opt_(InformationLength)
                                     PVOID Information,
                                 _In_ UINT32 InformationLength) {
  D3DKMT_QUERYADAPTERINFO queryAdapterInfo;

  memset(&queryAdapterInfo, 0, sizeof(D3DKMT_QUERYADAPTERINFO));
  queryAdapterInfo.hAdapter = AdapterHandle;
  queryAdapterInfo.Type = InformationClass;
  queryAdapterInfo.pPrivateDriverData = Information;
  queryAdapterInfo.PrivateDriverDataSize = InformationLength;

  return D3DKMTQueryAdapterInfo(&queryAdapterInfo);
}

BOOLEAN IsSoftwareDevice(_In_ D3DKMT_HANDLE AdapterHandle) {
  D3DKMT_ADAPTERTYPE adapterType;
  memset(&adapterType, 0, sizeof(D3DKMT_ADAPTERTYPE));

  if (NT_SUCCESS(QueryAdapterInformation(AdapterHandle, KMTQAITYPE_ADAPTERTYPE,
                                         &adapterType,
                                         sizeof(D3DKMT_ADAPTERTYPE)))) {
    if (adapterType.SoftwareDevice)  // adapterType.HybridIntegrated
    {
      return TRUE;
    }
  }

  return FALSE;
}
}  // namespace

namespace cil {

void PerformanceCounterDataNpuGpu::UpdateDeviceMemory() {
  GUID GUID_DISPLAY_DEVICE_ARRIVAL = {0x1CA05180,
                                      0xA699,
                                      0x450A,
                                      {0x9A, 0x0C, 0xDE, 0x4F, 0xBE, 0x3D, 0xDD,
                                       0x89}};  // GUID_DISPLAY_DEVICE_ARRIVAL
  GUID GUID_COMPUTE_DEVICE_ARRIVAL = {0x1024e4c9,
                                      0x47c9,
                                      0x48d3,
                                      {0xb4, 0xa8, 0xf9, 0xdf, 0x78, 0x52, 0x3b,
                                       0x53}};  // GUID_COMPUTE_DEVICE_ARRIVAL

  std::vector<GUID> guids = {GUID_DISPLAY_DEVICE_ARRIVAL,
                             GUID_COMPUTE_DEVICE_ARRIVAL};

  for (auto& guid : guids) {
    ULONG deviceInterfaceListSize = 0;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
        &deviceInterfaceListSize, &guid, nullptr,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

    if (cr == CR_SUCCESS) {
      std::vector<WCHAR> deviceInterfaceList(deviceInterfaceListSize);
      cr = CM_Get_Device_Interface_ListW(
          &guid, nullptr, deviceInterfaceList.data(), deviceInterfaceListSize,
          CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

      if (cr == CR_SUCCESS) {
        for (WCHAR* deviceInterface = deviceInterfaceList.data();
             *deviceInterface;
             deviceInterface +=
             wcsnlen(deviceInterface, deviceInterfaceListSize) + 1) {
          // std::wcout << L"Device: " << deviceInterface << std::endl;

          //
          DEVINST deviceInstance;
          if (GetDeviceInstance(deviceInterface, deviceInstance)) {
            // std::wcout << L"  Name: " <<
            // GetDevNodePropertyString(deviceInstance,
            // DEVPKEY_Device_DeviceDesc) << std::endl;
          }

          // Open the adapter using D3DKMTOpenAdapterFromDeviceName
          D3DKMT_OPENADAPTERFROMDEVICENAME openAdapter;
          ZeroMemory(&openAdapter, sizeof(openAdapter));
          openAdapter.pDeviceName = deviceInterface;

          if (D3DKMTOpenAdapterFromDeviceName(&openAdapter) != STATUS_SUCCESS) {
            LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                          "D3DKMTOpenAdapterFromDeviceName failed.");
            break;
          }

          auto it = std::find_if(
              devices_.begin(), devices_.end(), [&](const Device& d) {
                return memcmp(&d.luid, &openAdapter.AdapterLuid,
                              sizeof(LUID)) == 0;
              });
          if (it != devices_.end()) {
            // Use the adapter handle (openAdapter.hAdapter)
            // std::cout << "  Adapter Handle: " << openAdapter.hAdapter <<
            // std::endl;

            // Use the adapter LUID (openAdapter.AdapterLuid)
            // std::cout << "  Adapter LUID: "
            //    << std::uppercase << std::hex << std::setw(8) <<
            //    std::setfill('0') << openAdapter.AdapterLuid.HighPart << "-"
            //    << std::uppercase << std::setw(8) << std::setfill('0') <<
            //    openAdapter.AdapterLuid.LowPart << std::dec << std::endl;

            // std::cout << "  Type: " <<
            // (!IsSoftwareDevice(openAdapter.hAdapter) ? "Hardware" :
            // "Software") << std::endl;

            //
            D3DKMT_SEGMENTSIZEINFO segmentInfo;
            memset(&segmentInfo, 0, sizeof(D3DKMT_SEGMENTSIZEINFO));

            if (QueryAdapterInformation(openAdapter.hAdapter,
                                        KMTQAITYPE_GETSEGMENTSIZE, &segmentInfo,
                                        sizeof(D3DKMT_SEGMENTSIZEINFO)) !=
                STATUS_SUCCESS) {
              std::cerr << "D3DKMTQueryAdapterInfo failed." << std::endl;
            } else {
              it->memory.dedicated_video_memory =
                  segmentInfo.DedicatedVideoMemorySize;
              it->memory.dedicated_system_memory =
                  segmentInfo.DedicatedSystemMemorySize;
              it->memory.shared_system_memory =
                  segmentInfo.SharedSystemMemorySize;
            }
          }

          // Close the adapter (optional, but recommended)
          D3DKMT_CLOSEADAPTER closeAdapter({openAdapter.hAdapter});
          D3DKMTCloseAdapter(&closeAdapter);
        }
      } else {
        LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                      "CM_Get_Device_Interface_ListW failed: ");
      }
    } else {
      LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                    "CM_Get_Device_Interface_List_SizeW failed: " << cr);
    }
  }
}

}  // namespace cil