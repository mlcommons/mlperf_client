#include "ep_dependencies_manager.h"

#include <log4cxx/logger.h>

#include <filesystem>

#include "ep_dependencies_config.h"
#include "execution_provider.h"
#include "utils.h"

namespace fs = std::filesystem;
using namespace log4cxx;

LoggerPtr loggerEPDependenciesManager(Logger::getLogger("SystemController"));

namespace cil {

EPDependenciesManager::EPDependenciesManager(
    const std::unordered_map<std::string, std::string>& eps,
    const std::string& config_path, const std::string& config_schema_path,
    const std::string& deps_dir)
    : eps_(eps),
      config_path_(config_path),
      config_schema_path_(config_schema_path),
      deps_dir_(deps_dir) {}

EPDependenciesManager::~EPDependenciesManager() = default;

bool EPDependenciesManager::Initialize() {
  config_ = std::make_unique<EPDependenciesConfig>();

  if (!config_->ValidateAndParse(config_path_, config_schema_path_)) {
    LOG4CXX_ERROR(loggerEPDependenciesManager,
                  "Unable to load EP dependencies config");
    return false;
  }
  if (!config_->AreDependenciesResolved()) {
    LOG4CXX_ERROR(loggerEPDependenciesManager, "Dependencies are not resolved");
    return false;
  }

  for (const auto& [ep, ep_path] : eps_) {
    fs::path download_files_dir = fs::path(ep_path);

#if !WITH_IHV_NATIVE_OPENVINO
    if (ep == "NativeOpenVINO" || ep == "IHV NativeOpenVINO") continue;
#endif

#if !WITH_IHV_ORT_GENAI
    if (ep == "IHV OrtGenAi" || ep == "OrtGenAi") continue;
#endif

#if !WITH_IHV_GGML_METAL
    if (ep == "Metal" || ep == "IHV Metal") continue;
#endif
#if !WITH_IHV_GGML_VULKAN
    if (ep == "Vulkan" || ep == "IHV Vulkan") continue;
#endif
#if !WITH_IHV_GGML_CUDA
    if (ep == "CUDA" || ep == "IHV CUDA") continue;
#endif

#if !WITH_IHV_NATIVE_QNN
    if (ep == "NativeQNN" || ep == "IHV NativeQNN") continue;
#endif
#if !WITH_IHV_MLX
    if (ep == "MLX" || ep == "IHV MLX") continue;
#endif

    if (!fs::exists(download_files_dir) &&
        !fs::create_directories(download_files_dir)) {
      LOG4CXX_ERROR(loggerEPDependenciesManager,
                    "Unable to create directory " << download_files_dir);
      return false;
    }

    const auto& dependencies = config_->GetDependenciesForEP(ep);

    required_files_[ep] =
        std::make_pair(ep_path, std::unordered_set<std::string>());
    for (const auto& file : dependencies.GetFiles()) {
      const fs::path file_path = download_files_dir / file.first;
      // if (!fs::exists(file_path))
      required_files_[ep].second.insert(file.second);
    }
  }
  return true;
}

std::unordered_set<std::string> EPDependenciesManager::GetRequiredFiles()
    const {
  std::unordered_set<std::string> all_files;
  for (const auto& [ep, files_of_ep] : required_files_) {
    all_files.insert(files_of_ep.second.begin(), files_of_ep.second.end());
  }
  return all_files;
}

bool EPDependenciesManager::PrepareDependenciesForEP(
    const std::string& ep_name) {
  // Check if the EP is supported
  if (eps_.find(ep_name) == eps_.end()) {
    // EP not tracked by the dependencies manager
    return true;
  }
  const auto& dependencies = config_->GetDependenciesForEP(ep_name);
  bool success = true;
  for (const auto& file : dependencies.GetFiles()) {
    const fs::path file_path =
        fs::path(required_files_[ep_name].first) / file.first;
    if (!fs::exists(file_path)) {
      LOG4CXX_ERROR(loggerEPDependenciesManager,
                    "Unable to prepare ep dependency, name: "
                        << file.first << ", path: " << file.second);
      success = false;
    }
  }
  return success;
}

std::map<std::string, std::unordered_set<std::string>>
EPDependenciesManager::GetEpsStorageFiles() const {
  std::map<std::string, std::unordered_set<std::string>> eps_storage_files;
  for (const auto& [ep, files_of_ep] : required_files_) {
#if IGNORE_FILES_FROM_DISABLED_EPS && 1
    if (!cil::utils::IsEpSupportedOnThisPlatform("", ep)) continue;
#endif
    fs::path download_files_dir = files_of_ep.first;

    auto key = download_files_dir.string();
    if (eps_storage_files.find(key) == eps_storage_files.end()) {
      eps_storage_files[key] = std::unordered_set<std::string>();
    }
    eps_storage_files[key].insert(files_of_ep.second.begin(),
                                  files_of_ep.second.end());
  }
  return eps_storage_files;
}

}  // namespace cil