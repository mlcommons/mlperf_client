#include "system_info_provider_windows.h"

#include <Wbemidl.h>
#include <comdef.h>
#include <log4cxx/logger.h>

#include <iostream>
#include <locale>
#include <codecvt>

using namespace log4cxx;
LoggerPtr loggerSystemInfoProviderWindows(
    Logger::getLogger("SystemInfoProvider"));

namespace cil {

static std::string CPUArchitectureIDToString(int id) {
  switch (id) {
    case 0:
      return "x86";
    case 1:
      return "MIPS";
    case 2:
      return "Alpha";
    case 3:
      return "PowerPC";
    case 6:
      return "ia64";
    case 9:
      return "x64";
    case 12:
      return "ARM64";
    default:
      return "Unknown";
  }
}

SystemInfoProviderWindows::SystemInfoProviderWindows() : services_(nullptr) {
  Initialize();
}

SystemInfoProviderWindows::~SystemInfoProviderWindows() { Cleanup(); }

void SystemInfoProviderWindows::Initialize() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Failed to initialize COM library, error code: " << hr);
    return;
  }

  hr = CoInitializeSecurity(
      nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Failed to initialize security, error code: " << hr);
    CoUninitialize();
    return;
  }

  IWbemLocator* locator = nullptr;
  hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                        IID_IWbemLocator, reinterpret_cast<void**>(&locator));
  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Failed to create IWbemLocator object, error code: " << hr);
    CoUninitialize();
    return;
  }

  hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0,
                              NULL, 0, 0, &services_);

  locator->Release();
  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Could not connect to WMI server, error code: " << hr);
    CoUninitialize();
  }
}

void SystemInfoProviderWindows::Cleanup() {
  if (services_) {
    services_->Release();
    services_ = nullptr;
  }
  CoUninitialize();
}

void SystemInfoProviderWindows::FetchCpuInfo() {
  if (!services_) return;
  IEnumWbemClassObject* enumerator = nullptr;
  HRESULT hr = services_->ExecQuery(
      bstr_t("WQL"),
      bstr_t("SELECT Architecture, Name, Manufacturer, NumberOfCores, "
             "NumberOfLogicalProcessors, MaxClockSpeed FROM Win32_Processor"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Query for CPU information failed, error code: " << hr);
    return;
  }
  IWbemClassObject* class_object = nullptr;
  ULONG u_return = 0;
  if (enumerator->Next(WBEM_INFINITE, 1, &class_object, &u_return) ==
      WBEM_S_NO_ERROR) {
    VARIANT var;
    memset(&var, 0, sizeof(VARIANT));

    class_object->Get(L"Architecture", 0, &var, nullptr, nullptr);
    cpu_info_.architecture = CPUArchitectureIDToString(var.intVal);
    VariantClear(&var);

    class_object->Get(L"Name", 0, &var, nullptr, nullptr);
    cpu_info_.model_name = _bstr_t(var.bstrVal, false);
    if (cpu_info_.architecture == "ARM64") {
      cpu_info_.model_name = cpu_info_.model_name.substr(0,10);
    }
    VariantClear(&var);

    class_object->Get(L"Manufacturer", 0, &var, nullptr, nullptr);
    cpu_info_.vendor_id = _bstr_t(var.bstrVal, false);
    VariantClear(&var);

    class_object->Get(L"NumberOfCores", 0, &var, nullptr, nullptr);
    cpu_info_.physical_cpus = var.uintVal;
    VariantClear(&var);

    class_object->Get(L"NumberOfLogicalProcessors", 0, &var, nullptr, nullptr);
    cpu_info_.logical_cpus = var.uintVal;
    VariantClear(&var);

    class_object->Get(L"MaxClockSpeed", 0, &var, nullptr, nullptr);
    cpu_info_.frequency = var.uintVal * 1000000;  // Convert MHz to Hz
    VariantClear(&var);

    class_object->Release();
  }
  enumerator->Release();
}

void SystemInfoProviderWindows::FetchGpuInfo() {
  if (!services_) return;
  IEnumWbemClassObject* enumerator = nullptr;
  HRESULT
  hr = services_->ExecQuery(
      _bstr_t("WQL"),
      _bstr_t("SELECT Name, AdapterCompatibility, DriverVersion, AdapterRAM "
              "FROM Win32_VideoController"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Query for video controllers failed, error code: " << hr);
    return;
  }
  IWbemClassObject* class_object = nullptr;
  ULONG u_return = 0;
  while (enumerator->Next(WBEM_INFINITE, 1, &class_object, &u_return) ==
         WBEM_S_NO_ERROR) {
    GPUInfo gpu_info;

    VARIANT var;
    memset(&var, 0, sizeof(VARIANT));

    class_object->Get(L"Name", 0, &var, nullptr, nullptr);
    gpu_info.name = _bstr_t(var.bstrVal, false);
    VariantClear(&var);

    class_object->Get(L"AdapterCompatibility", 0, &var, nullptr, nullptr);
    gpu_info.vendor = _bstr_t(var.bstrVal, false);
    VariantClear(&var);

    class_object->Get(L"DriverVersion", 0, &var, nullptr, nullptr);
    gpu_info.driver_version = _bstr_t(var.bstrVal, false);
    VariantClear(&var);

    class_object->Get(L"AdapterRAM", 0, &var, nullptr, nullptr);
    gpu_info.memory_size = var.ullVal;  // In bytes
    VariantClear(&var);

    gpu_info_.push_back(gpu_info);

    class_object->Release();
  }
  enumerator->Release();
}

void SystemInfoProviderWindows::FetchSystemMemoryInfo() {
  if (!services_) return;
  IEnumWbemClassObject* enumerator = nullptr;
  HRESULT hr = services_->ExecQuery(
      bstr_t("WQL"),
      bstr_t("SELECT TotalVisibleMemorySize, FreePhysicalMemory FROM "
             "Win32_OperatingSystem"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);

  if (FAILED(hr)) {
    LOG4CXX_ERROR(
        loggerSystemInfoProviderWindows,
        "Query for system memory information failed, error code: " << hr);
    return;
  }
  IWbemClassObject* class_object = nullptr;
  ULONG u_return = 0;
  if (enumerator->Next(WBEM_INFINITE, 1, &class_object, &u_return) ==
      WBEM_S_NO_ERROR) {
    VARIANT var;
    memset(&var, 0, sizeof(VARIANT));

    class_object->Get(L"TotalVisibleMemorySize", 0, &var, nullptr, nullptr);
    memory_info_.physical_total = std::atoll(bstr_t(var.bstrVal)) * 1024;
    VariantClear(&var);

    class_object->Get(L"FreePhysicalMemory", 0, &var, nullptr, nullptr);
    memory_info_.physical_available = std::atoll(bstr_t(var.bstrVal)) * 1024;
    VariantClear(&var);

    class_object->Release();
  }
  enumerator->Release();
}

void SystemInfoProviderWindows::FetchOsInfo() {
  if (!services_) return;
  IEnumWbemClassObject* enumerator = nullptr;
  HRESULT hr = services_->ExecQuery(
      bstr_t("WQL"),
      bstr_t("SELECT Caption, Version, BuildNumber FROM Win32_OperatingSystem"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Query for OS information failed, error code: " << hr);
  }
  IWbemClassObject* class_object = nullptr;
  ULONG u_return = 0;
  if (enumerator->Next(WBEM_INFINITE, 1, &class_object, &u_return) ==
      WBEM_S_NO_ERROR) {
    VARIANT var;
    memset(&var, 0, sizeof(VARIANT));

    class_object->Get(L"Caption", 0, &var, nullptr, nullptr);
    _bstr_t caption(var.bstrVal, false);
    // Convert _bstr_t (UTF-16) to std::wstring
    std::wstring ws_caption(static_cast<wchar_t*>(caption));
    // Convert std::wstring (UTF-16) to std::string (UTF-8)
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    os_info_.full_name = converter.to_bytes(ws_caption);
    VariantClear(&var);

    class_object->Get(L"Version", 0, &var, nullptr, nullptr);
    os_info_.version = _bstr_t(var.bstrVal, false) + " Build ";
    VariantClear(&var);

    class_object->Get(L"BuildNumber", 0, &var, nullptr, nullptr);
    os_info_.version += _bstr_t(var.bstrVal, false);
    VariantClear(&var);

    class_object->Release();
  }
  enumerator->Release();
}

}  // namespace cil