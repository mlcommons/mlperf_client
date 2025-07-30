#include "system_info_provider_windows.h"

#include <Wbemidl.h>
#include <comdef.h>
#include <log4cxx/logger.h>
#include <powerbase.h>
#include <powrprof.h>

#include <ranges>

#include "performance_counter_Group.h"
#include "performance_counter_data.h"
#include "performance_query.h"

#pragma comment(lib, "powrprof.lib")

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

SystemInfoProviderWindows::SystemInfoProviderWindows() : services_(nullptr) {}

SystemInfoProviderWindows::~SystemInfoProviderWindows() { CleanupWbem(); }

void SystemInfoProviderWindows::InitializeWbem() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Failed to initialize COM library, error code: " << hr);
    return;
  }

  hr = CoInitializeSecurity(
      nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
  if (FAILED(hr)) {
    if (hr == RPC_E_TOO_LATE) {
      LOG4CXX_ERROR(
          loggerSystemInfoProviderWindows,
          "CoInitializeSecurity was already called, error code: " << hr);
    } else {
      LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                    "Failed to initialize security, error code: " << hr);
      CoUninitialize();
      return;
    }
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

  hr = CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                         nullptr, RPC_C_AUTHN_LEVEL_CALL,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Could not set proxy blanket, error code: " << hr);
    services_->Release();
    services_ = nullptr;
    CoUninitialize();
  }
}

void SystemInfoProviderWindows::CleanupWbem() {
  if (services_) {
    services_->Release();
    services_ = nullptr;
    CoUninitialize();
  }
}

void SystemInfoProviderWindows::FetchInfo() {
  InitializeWbem();
  SystemInfoProvider::FetchInfo();
  CleanupWbem();
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
      cpu_info_.model_name = cpu_info_.model_name.substr(0, 10);
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
      _bstr_t("SELECT Name, AdapterCompatibility, DriverVersion, AdapterRAM, "
              "PNPDeviceID "
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

    class_object->Get(L"PNPDeviceID", 0, &var, nullptr, nullptr);
    std::string pnp_device_id;
    pnp_device_id = _bstr_t(var.bstrVal, false);
    VariantClear(&var);

    // Skip virtual devices: physical GPUs have "VEN_" in the second part of
    // PNPDeviceID
    auto splitted = std::views::split(pnp_device_id, '\\');
    if (auto it = splitted.begin();
        it == splitted.end() || ++it == splitted.end() ||
        !std::string_view((*it).begin(), (*it).end()).starts_with("VEN_"))
      continue;

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
    gpu_info.dedicated_memory_size = var.ullVal;  // In bytes
    VariantClear(&var);

    gpu_info_.push_back(gpu_info);

    class_object->Release();
  }
  enumerator->Release();

  if (!performance_query_) InitializePerformanceCounters();
  auto data_npu_gpu =
      (PerformanceCounterDataNpuGpu*)npu_gpu_counters_->GetData();
  for (int i = 0; i < data_npu_gpu->GetDeviceCount(); i++) {
    auto it = std::find_if(
        gpu_info_.begin(), gpu_info_.end(), [&](const GPUInfo& info) {
          return std::string(_bstr_t(data_npu_gpu->GetDeviceName(i).c_str())) ==
                 info.name;
        });
    if (it != gpu_info_.end()) {
      it->dedicated_memory_size =
          data_npu_gpu->GetDeviceMemory(i).dedicated_video_memory;
      it->shared_memory_size =
          data_npu_gpu->GetDeviceMemory(i).shared_system_memory;
    }
  }
}

void SystemInfoProviderWindows::FetchNpuInfo() {
  if (!services_) return;
  IEnumWbemClassObject* enumerator = nullptr;

  HRESULT hr = services_->ExecQuery(
      _bstr_t("WQL"),
      _bstr_t("SELECT Name, Manufacturer FROM Win32_PnPEntity WHERE PNPClass = "
              "'ComputeAccelerator'"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);

  if (FAILED(hr)) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "Query for compute accelerators failed, error code: " << hr);
    return;
  }

  IWbemClassObject* class_object = nullptr;
  ULONG u_return = 0;

  while (enumerator->Next(WBEM_INFINITE, 1, &class_object, &u_return) ==
         WBEM_S_NO_ERROR) {
    VARIANT var;
    VariantInit(&var);

    // Name
    if (SUCCEEDED(class_object->Get(L"Name", 0, &var, nullptr, nullptr)) &&
        var.vt == VT_BSTR) {
      npu_info_.name = _bstr_t(var.bstrVal, false);
    } else {
      continue;
    }
    VariantClear(&var);

    // Manufacturer
    if (SUCCEEDED(
            class_object->Get(L"Manufacturer", 0, &var, nullptr, nullptr)) &&
        var.vt == VT_BSTR) {
      npu_info_.vendor = _bstr_t(var.bstrVal, false);
    } else {
      continue;
    }
    VariantClear(&var);

    class_object->Release();
    break;
  }

  enumerator->Release();

  if (!performance_query_) InitializePerformanceCounters();
  auto data_npu_gpu =
      (PerformanceCounterDataNpuGpu*)npu_gpu_counters_->GetData();
  for (int i = 0; i < data_npu_gpu->GetDeviceCount(); i++) {
    if (std::string(_bstr_t(data_npu_gpu->GetDeviceName(i).c_str())) ==
        npu_info_.name) {
      npu_info_.dedicated_memory_size =
          data_npu_gpu->GetDeviceMemory(i).dedicated_video_memory;
      npu_info_.shared_memory_size =
          data_npu_gpu->GetDeviceMemory(i).shared_system_memory;
      break;
    }
  }
}

void SystemInfoProviderWindows::FetchSystemMemoryInfo() {
  if (!services_) return;
  IEnumWbemClassObject* enumerator = nullptr;
  HRESULT hr = services_->ExecQuery(
      bstr_t("WQL"),
      bstr_t("SELECT TotalVisibleMemorySize, TotalVirtualMemorySize, "
             "FreePhysicalMemory FROM "
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

    class_object->Get(L"TotalVirtualMemorySize", 0, &var, nullptr, nullptr);
    memory_info_.virtual_total = std::atoll(bstr_t(var.bstrVal)) * 1024;
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
    os_info_.full_name = _bstr_t(var.bstrVal, false);
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

void SystemInfoProviderWindows::FetchPerformanceInfo() {
  if (!performance_query_) InitializePerformanceCounters();

  performance_query_->Query();
  performance_query_->FormatCounters();

  // CPU
  bool exists = false;
  performance_info_.cpu_usage = cpu_counters_->GetValue(0, "Usage", &exists);
  performance_info_.cpu_temperature = 50;
  performance_info_.cpu_temperature_is_available = false;
  performance_info_.memory_usage =
      GetMemoryInfo().physical_total -
      cpu_counters_->GetValue(0, "Memory", &exists);
  performance_info_.power_scheme = GetCurrentPowerScheme();
  performance_info_.power_scheme_is_available = true;

  // NPU/GPU
  auto data = npu_gpu_counters_->GetData();
  for (int i = 0; i < data->GetDeviceCount(); i++) {
    double usage_3D = npu_gpu_counters_->GetValue(i, "Usage: 3D", &exists);
    double usage_compute =
        npu_gpu_counters_->GetValue(i, "Usage: Compute", &exists);
    double usage_copy = npu_gpu_counters_->GetValue(i, "Usage: Copy", &exists);
    double dedicated_memory_usage =
        npu_gpu_counters_->GetValue(i, "Dedicated Memory", &exists);
    double shared_memory_usage =
        npu_gpu_counters_->GetValue(i, "Shared Memory", &exists);

    NPUGPUPerformanceInfo info = {
        std::string(_bstr_t(data->GetDeviceName(i).c_str())),
        static_cast<size_t>(max(max(usage_3D, usage_compute), usage_copy)),
        50,
        false,
        static_cast<size_t>(dedicated_memory_usage),
        static_cast<size_t>(shared_memory_usage)};

    if (data->GetDeviceType(i) == "NPU") {
      performance_info_.npu_info = info;
    } else {
      if (std::ranges::find_if(gpu_info_, [&](const GPUInfo& gpu_info) {
            return gpu_info.name == info.name;
          }) != gpu_info_.end())
        performance_info_.gpu_info[info.name] = info;
    }
  }
}

void SystemInfoProviderWindows::InitializePerformanceCounters() {
  cpu_counters_ = std::make_shared<PerformanceCounterGroup>(
      "CPU", std::make_unique<PerformanceCounterData>());
  cpu_counters_->AddCounter("Usage", "%",
                            "\\Processor(_Total)\\% Processor Time");
  cpu_counters_->AddCounter("Memory", "Bytes", "\\Memory\\Available Bytes",
                            1.0);

  npu_gpu_counters_ = std::make_shared<PerformanceCounterArrayGroup>(
      "NPU/GPU", std::make_unique<PerformanceCounterDataNpuGpu>());
  npu_gpu_counters_->AddCounter("Usage: 3D", "%",
                                "\\GPU Engine(*)\\Utilization Percentage", 1.0,
                                L"engtype_3D");
  npu_gpu_counters_->AddCounter("Usage: Compute", "%",
                                "\\GPU Engine(*)\\Utilization Percentage", 1.0,
                                L"engtype_Compute");
  npu_gpu_counters_->AddCounter("Usage: Copy", "%",
                                "\\GPU Engine(*)\\Utilization Percentage", 1.0,
                                L"engtype_Copy");
  npu_gpu_counters_->AddCounter("Dedicated Memory", "Bytes",
                                "\\GPU Adapter Memory(*)\\Dedicated Usage",
                                1.0);
  npu_gpu_counters_->AddCounter("Shared Memory", "Gb",
                                "\\GPU Adapter Memory(*)\\Shared Usage", 1.0);

  performance_query_ = std::make_unique<PerformanceQuery>();
  performance_query_->Open();
  performance_query_->AddCounterGroup(cpu_counters_);
  performance_query_->AddCounterGroup(npu_gpu_counters_);
  performance_query_->Query();
}

std::string SystemInfoProviderWindows::GetCurrentPowerScheme() {
  GUID* active_scheme = nullptr;
  DWORD size = 0;
  std::string scheme_name = "Unknown";
  if (PowerGetActiveScheme(nullptr, &active_scheme) != ERROR_SUCCESS) {
    return scheme_name;
  }

  if (PowerReadFriendlyName(nullptr, active_scheme, nullptr, nullptr, nullptr,
                            &size) != ERROR_SUCCESS) {
    LocalFree(active_scheme);
    return scheme_name;
  }

  std::vector<UCHAR> buffer(size);
  if (PowerReadFriendlyName(nullptr, active_scheme, nullptr, nullptr,
                            buffer.data(), &size) != ERROR_SUCCESS) {
    LocalFree(active_scheme);
    return scheme_name;
  }
  auto wide_str = reinterpret_cast<wchar_t*>(buffer.data());
  int buffer_size = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, nullptr, 0,
                                        nullptr, nullptr);

  if (buffer_size > 0) {
    std::vector<char> utf8_buffer(buffer_size);
    WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, utf8_buffer.data(),
                        buffer_size, nullptr, nullptr);
    scheme_name = std::string(utf8_buffer.data());
  }
  LocalFree(active_scheme);
  return scheme_name;
}

}  // namespace cil