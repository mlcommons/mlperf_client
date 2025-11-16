#include "system_info_provider_linux.h"

#include <log4cxx/logger.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <array>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <cstring>

using namespace log4cxx;
LoggerPtr loggerSystemInfoProviderLinux(
    Logger::getLogger("SystemInfoProvider"));

namespace cil {

void SystemInfoProviderLinux::FetchCpuInfo() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (!cpuinfo.is_open()) {
        LOG4CXX_ERROR(loggerSystemInfoProviderLinux, "Failed to open /proc/cpuinfo");
        return;
    }

    std::string line;
    bool first_cpu = true;
    while (std::getline(cpuinfo, line)) {
        if (line.empty()) {
            first_cpu = false;
            continue;
        }

        if (first_cpu) {
            if (line.find("model name") != std::string::npos) {
                cpu_info_.model_name = line.substr(line.find(":") + 2);
            } else if (line.find("vendor_id") != std::string::npos) {
                cpu_info_.vendor_id = line.substr(line.find(":") + 2);
            } else if (line.find("cpu MHz") != std::string::npos) {
                std::string freq_str = line.substr(line.find(":") + 2);
                cpu_info_.frequency = static_cast<uint64_t>(std::stod(freq_str) * 1000000);
            }
        }

        if (line.find("processor") != std::string::npos) {
            cpu_info_.logical_cpus++;
        }
    }

    // Get physical cores
    std::ifstream cpu_present("/sys/devices/system/cpu/present");
    if (cpu_present.is_open()) {
        std::string present;
        cpu_present >> present;
        // Count the number of physical cores
        std::regex range_regex("(\\d+)-(\\d+)");
        std::smatch matches;
        if (std::regex_search(present, matches, range_regex)) {
            int start = std::stoi(matches[1]);
            int end = std::stoi(matches[2]);
            cpu_info_.physical_cpus = (end - start + 1) / 2; // Assuming hyperthreading
        }
    }

    // Get architecture
    struct utsname uname_data;
    if (uname(&uname_data) == 0) {
        std::string machine = uname_data.machine;
        if (machine == "x86_64") {
            cpu_info_.architecture = "x64";
        } else if (machine == "aarch64") {
            cpu_info_.architecture = "ARM64";
        } else if (machine == "i686" || machine == "i386") {
            cpu_info_.architecture = "x86";
        } else {
            cpu_info_.architecture = machine;
        }
    }
}

void SystemInfoProviderLinux::FetchGpuInfo() {
    // Scan PCI devices in sysfs
    DIR* dir = opendir("/sys/bus/pci/devices");
    if (!dir) {
        LOG4CXX_ERROR(loggerSystemInfoProviderLinux, "Failed to open PCI devices directory");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string device_path = "/sys/bus/pci/devices/" + std::string(entry->d_name);
        
        // Check if this is a display controller
        std::ifstream class_file(device_path + "/class");
        if (!class_file.is_open()) continue;

        uint32_t class_code;
        class_file >> std::hex >> class_code;
        if ((class_code >> 16) != 0x03) continue; // Not a display controller

        GPUInfo gpu_info;

        // Get vendor and device names
        std::ifstream vendor_file(device_path + "/vendor");
        std::ifstream device_file(device_path + "/device");
        if (vendor_file.is_open() && device_file.is_open()) {
            uint32_t vendor_id, device_id;
            vendor_file >> std::hex >> vendor_id;
            device_file >> std::hex >> device_id;

            // Map common vendor IDs to names
            switch (vendor_id) {
                case 0x1002: gpu_info.vendor = "AMD"; break;
                case 0x10DE: gpu_info.vendor = "NVIDIA"; break;
                case 0x8086: gpu_info.vendor = "Intel"; break;
                default: gpu_info.vendor = "Unknown";
            }

            // Get device name from modalias
            std::ifstream modalias_file(device_path + "/modalias");
            if (modalias_file.is_open()) {
                std::string modalias;
                std::getline(modalias_file, modalias);
                size_t name_start = modalias.find(":p");
                if (name_start != std::string::npos) {
                    gpu_info.name = modalias.substr(name_start + 2);
                }
            }
        }

        // Get driver version
        std::ifstream driver_version_file(device_path + "/driver/module/version");
        if (driver_version_file.is_open()) {
            std::getline(driver_version_file, gpu_info.driver_version);
        }

        // Get memory size from resource file
        std::ifstream resource_file(device_path + "/resource");
        if (resource_file.is_open()) {
            std::string line;
            while (std::getline(resource_file, line)) {
                if (line.find("prefetchable") != std::string::npos) {
                    std::istringstream iss(line);
                    uint64_t start, end;
                    iss >> std::hex >> start >> end;
                    gpu_info.dedicated_memory_size = end - start + 1;
                    gpu_info.shared_memory_size = 0;  // Shared memory size is not available from PCI resources
                    
                    break;
                }
            }
        }

        gpu_info_.push_back(gpu_info);
    }

    closedir(dir);
}

void SystemInfoProviderLinux::FetchSystemMemoryInfo() {
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        LOG4CXX_ERROR(loggerSystemInfoProviderLinux, "Failed to get system memory info");
        return;
    }

    memory_info_.physical_total = si.totalram * si.mem_unit;
    memory_info_.physical_available = si.freeram * si.mem_unit;
}

void SystemInfoProviderLinux::FetchOsInfo() {
    struct utsname uname_data;
    if (uname(&uname_data) == 0) {
        os_info_.full_name = "Linux " + std::string(uname_data.release);
        os_info_.version = std::string(uname_data.release) + " " + std::string(uname_data.version);
    }

    // Try to get more detailed OS info from /etc/os-release
    std::ifstream os_release("/etc/os-release");
    if (os_release.is_open()) {
        std::string line;
        while (std::getline(os_release, line)) {
            if (line.find("PRETTY_NAME=") != std::string::npos) {
                os_info_.full_name = line.substr(line.find("=") + 1);
                // Remove quotes if present
                if (os_info_.full_name.front() == '"' && os_info_.full_name.back() == '"') {
                    os_info_.full_name = os_info_.full_name.substr(1, os_info_.full_name.length() - 2);
                }
                break;
            }
        }
    }
}

void SystemInfoProviderLinux::FetchNpuInfo() {
  // Linux doesn't have a standard way to detect NPUs
  // Set default values
  npu_info_.name = "Unknown";
  npu_info_.vendor = "Unknown";
  npu_info_.dedicated_memory_size = 0;
  npu_info_.shared_memory_size = 0;
}

void SystemInfoProviderLinux::FetchPerformanceInfo() {
  // Set default values for performance info
  performance_info_.cpu_usage = 0;
  performance_info_.cpu_temperature = 0.0;
  performance_info_.memory_usage = 0;
  performance_info_.power_scheme = "Unknown";
  
  // Clear GPU info
  performance_info_.gpu_info.clear();
  
  // Set default NPU info
  performance_info_.npu_info.name = "Unknown";
  performance_info_.npu_info.usage = 0;
  performance_info_.npu_info.temperature = 0.0;
  performance_info_.npu_info.dedicated_memory_usage = 0;
  performance_info_.npu_info.shared_memory_usage = 0;
}

} // namespace cil
