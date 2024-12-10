#include "utils_directml.h"

#include "utils.h"

#ifdef WIN32

#include <directml.h>
#include <dxcore.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "scope_exit.h"

namespace cil {
namespace utils {
namespace DirectML {

template <typename T>
T GetFunctionPointer(HMODULE hModule, const char* functionName) {
  T func = reinterpret_cast<T>(GetProcAddress(hModule, functionName));
  return func;
}

std::string GetDXCoreAdapterType(
    Microsoft::WRL::ComPtr<IDXCoreAdapter> adapter) {
  bool is_hardware = false;
  adapter->GetProperty(DXCoreAdapterProperty::IsHardware, &is_hardware);
  if (!is_hardware) return "";

  // GUID DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS;
  GUID attributes[] = {0x0c9ece4d, 0x2f6e, 0x4f01, 0x8c, 0x96, 0xe8,
                       0x9e,       0x33,   0x1b,   0x47, 0xb1};
  if (adapter->IsAttributeSupported(*attributes)) return "GPU";

  return "NPU";
}

std::string GetDXCoreAdapterType(
    const std::vector<std::pair<LUID, Microsoft::WRL::ComPtr<IDXCoreAdapter>>>&
        adapters,
    LUID luid) {
  auto adapter = std::find_if(
      adapters.begin(), adapters.end(),
      [&](const std::pair<LUID, Microsoft::WRL::ComPtr<IDXCoreAdapter>>& elem) {
        return elem.first.LowPart == luid.LowPart &&
               elem.first.HighPart == luid.HighPart;
      });

  return (adapter != adapters.end()) ? GetDXCoreAdapterType(adapter->second)
                                     : "";
}

std::vector<AdapterInfo> EnumerateDirectMLAdapters(
    const std::string& directml_directory) {
  cil::utils::LibraryPathHandle path_handle;

  if (!directml_directory.empty()) {
    path_handle = cil::utils::AddLibraryPath(directml_directory);
    if (!path_handle.IsValid()) return {};
  };

  ScopeExit library_path_cleanup([path_handle]() {
    if (path_handle.IsValid()) cil::utils::RemoveLibraryPath(path_handle);
  });

  decltype(&DMLCreateDevice1) DMLCreateDevice_ptr = nullptr;
  decltype(&D3D12CreateDevice) D3D12CreateDevice_ptr = nullptr;
  ScopeExit direct_ml_cleanup;
  ScopeExit d3d12_cleanup;

  HMODULE hDirectML = LoadLibraryA("DirectML.dll");
  if (!hDirectML) return {};
  direct_ml_cleanup.SetCallback([hDirectML]() { FreeLibrary(hDirectML); });

  DMLCreateDevice_ptr = GetFunctionPointer<decltype(&DMLCreateDevice1)>(
      hDirectML, "DMLCreateDevice1");
  if (!DMLCreateDevice_ptr) return {};

  HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
  if (!hD3D12) return {};
  d3d12_cleanup.SetCallback([hD3D12]() { FreeLibrary(hD3D12); });

  D3D12CreateDevice_ptr = GetFunctionPointer<decltype(&D3D12CreateDevice)>(
      hD3D12, "D3D12CreateDevice");
  if (!D3D12CreateDevice_ptr) return {};

  HMODULE hDXGI = LoadLibraryA("dxgi.dll");
  if (!hDXGI) return {};

  ScopeExit dxgi_cleanup([hDXGI]() { FreeLibrary(hDXGI); });

  auto CreateDXGIFactory1_ptr =
      GetFunctionPointer<decltype(&CreateDXGIFactory1)>(hDXGI,
                                                        "CreateDXGIFactory1");
  if (!CreateDXGIFactory1_ptr) return {};

  IDXGIFactory4* pFactory = nullptr;
  HRESULT hr = CreateDXGIFactory1_ptr(IID_PPV_ARGS(&pFactory));
  if (FAILED(hr)) return {};

  std::unordered_map<UINT,
                     std::unordered_map<UINT, std::pair<UINT, AdapterInfo>>>
      adapters_map;

  for (UINT adapterIndex = 0;; ++adapterIndex) {
    IDXGIAdapter1* pAdapter = nullptr;
    hr = pFactory->EnumAdapters1(adapterIndex, &pAdapter);
    if (FAILED(hr)) break;  // No more adapters

    ScopeExit adapter_cleanup([pAdapter]() { pAdapter->Release(); });

    DXGI_ADAPTER_DESC1 desc;
    pAdapter->GetDesc1(&desc);

    std::wstring wname(desc.Description, std::char_traits<WCHAR>::length(desc.Description));
    std::string adapterName(wname.begin(), wname.end());
    adapterName = CleanAndTrimString(adapterName);

    auto is_basic_render_driver_vendor_id = desc.VendorId == 0x1414;
    auto is_basic_render_driver_device_id = desc.DeviceId == 0x8c;
    auto is_software_adapter = desc.Flags == DXGI_ADAPTER_FLAG_SOFTWARE;

    if (is_software_adapter ||
        (is_basic_render_driver_vendor_id && is_basic_render_driver_device_id))
      continue;

    ID3D12Device* pD3D12Device = nullptr;
    hr = D3D12CreateDevice_ptr(pAdapter, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&pD3D12Device));
    if (FAILED(hr)) continue;

    auto dml_feature_level_5_0 = (DML_FEATURE_LEVEL)0x5000;

    IDMLDevice* pDMLDevice = nullptr;
    hr = DMLCreateDevice_ptr(pD3D12Device, DML_CREATE_DEVICE_FLAG_NONE,
                             dml_feature_level_5_0, IID_PPV_ARGS(&pDMLDevice));

    if (SUCCEEDED(hr)) {
      AdapterInfo info = {adapterName, "GPU", desc.AdapterLuid.LowPart,
                          desc.AdapterLuid.HighPart};

      if (!adapters_map.contains(desc.VendorId))
        adapters_map[desc.VendorId] = {};
      else if (adapters_map[desc.VendorId].contains(desc.DeviceId))
        continue;

      adapters_map[desc.VendorId][desc.DeviceId] = {adapterIndex, info};

      pDMLDevice->Release();
    }

    pD3D12Device->Release();
  }

  pFactory->Release();

  std::vector<AdapterInfo> adapters;
  UINT max_device_id = 0;

  for (const auto& [vendor, devices] : adapters_map) {
    for (const auto& [device, adapter] : devices) {
      max_device_id = std::max(max_device_id, adapter.first);
    }
  }

  adapters.resize(max_device_id + 1);

  for (const auto& [vendor, devices] : adapters_map) {
    for (const auto& [device, adapter] : devices) {
      adapters[adapter.first] = adapter.second;
    }
  }

  return adapters;
}

std::vector<AdapterInfo> EnumerateDXCoreAdapters(
    const std::string& dxcore_directory) {
  cil::utils::LibraryPathHandle path_handle;

  if (!dxcore_directory.empty()) {
    path_handle = cil::utils::AddLibraryPath(dxcore_directory);
    if (!path_handle.IsValid()) return {};
  };

  ScopeExit library_path_cleanup([path_handle]() {
    if (path_handle.IsValid()) cil::utils::RemoveLibraryPath(path_handle);
  });

  HMODULE hDXCore = LoadLibraryA("DXCore.dll");
  if (!hDXCore) return {};

  ScopeExit dxcore_cleanup([hDXCore]() { FreeLibrary(hDXCore); });

  auto DXCoreCreateAdapterFactory_ptr =
      GetFunctionPointer<HRESULT (*)(REFIID, _COM_Outptr_ void**)>(
          hDXCore, "DXCoreCreateAdapterFactory");
  if (!DXCoreCreateAdapterFactory_ptr) return {};

  Microsoft::WRL::ComPtr<IDXCoreAdapterFactory> factory;
  HRESULT hr = DXCoreCreateAdapterFactory_ptr(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return {};

  Microsoft::WRL::ComPtr<IDXCoreAdapterList> adapter_list;

  // GUID DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC
  // (The NPU won't be enumerated otherwise)
  GUID attributes[] = {0xb71b0d41, 0x1088, 0x422f, 0xa2, 0x7c, 0x2,
                       0x50,       0xb7,   0xd3,   0xa9, 0x88};

  hr = factory->CreateAdapterList(_countof(attributes), attributes,
                                  IID_PPV_ARGS(&adapter_list));
  if (FAILED(hr)) return {};

  size_t adapter_count = adapter_list->GetAdapterCount();

  std::vector<AdapterInfo> adapters;
  adapters.reserve(adapter_count);

  for (size_t i = 0; i < adapter_count; ++i) {
    // If we are using DxCore we cannot use the DXGI interface to enumerate
    // the devices because DXGI interface is implicitly for enumerating
    // adapters that can be used for render and present, so including NPUs
    // would be a compatibility risk. That's why DxCore was spun off as a
    // separate agnostic component.

    Microsoft::WRL::ComPtr<IDXCoreAdapter> core_adapter;
    if (FAILED(adapter_list->GetAdapter(i, IID_PPV_ARGS(&core_adapter))))
      continue;

    LUID luid = {};
    hr = core_adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, &luid);

    if (FAILED(hr)) continue;

    std::string adapterType = GetDXCoreAdapterType(core_adapter);

    size_t bufSize = 0;
    core_adapter->GetPropertySize(DXCoreAdapterProperty::DriverDescription,
                                  &bufSize);

    std::string driver_name(bufSize, 0);
    core_adapter->GetProperty(DXCoreAdapterProperty::DriverDescription, bufSize,
                              driver_name.data());
    std::string adapterName = CleanAndTrimString(driver_name);

    DWORD AdapterLuidLowPart = luid.LowPart;
    DWORD AdapterLuidHighPart = luid.HighPart;

    AdapterInfo info = {adapterName, adapterType, AdapterLuidLowPart,
                        AdapterLuidHighPart};

    adapters.push_back(info);
  }

  return adapters;
}

std::vector<AdapterInfo> EnumerateAdapters(const std::string& dlls_directory) {
  DLL_DIRECTORY_COOKIE dll_cookie;

  auto dxcore_adapters = EnumerateDXCoreAdapters(dlls_directory);
  if (!dxcore_adapters.empty()) return dxcore_adapters;

  return EnumerateDirectMLAdapters(dlls_directory);
}

}  // namespace DirectML
}  // namespace utils
}  // namespace cil

#endif