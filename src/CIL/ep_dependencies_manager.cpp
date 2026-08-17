#include "ep_dependencies_manager.h"

#include <log4cxx/logger.h>

#include <filesystem>

#include "benchmark/runner.h"
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
    const std::string& deps_dir,
    const std::unordered_map<std::string, std::vector<nlohmann::json>>&
        ep_configs)
    : eps_(eps),
      config_path_(config_path),
      config_schema_path_(config_schema_path),
      deps_dir_(deps_dir),
      ep_configs_(ep_configs) {}

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

#if !WITH_IHV_GGML_METAL && !WITH_IHV_GGML_VULKAN && !WITH_IHV_GGML_CUDA && \
    !WITH_IHV_GGML_ROCM
    if (ep == "llama-cpp" || ep == "IHV LlamaCpp") continue;
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
    std::vector<nlohmann::json> configs_for_ep;
    if (const auto cfg_it = ep_configs_.find(ep); cfg_it != ep_configs_.end()) {
      configs_for_ep = cfg_it->second;
    }
    const auto filtered_files = dependencies.FilterFiles(configs_for_ep);

    required_files_[ep] =
        std::make_pair(ep_path, std::unordered_set<std::string>());
    auto& names = resolved_file_names_[ep];
    for (const auto& file : filtered_files) {
      const fs::path file_path = download_files_dir / file.name;
      // if (!fs::exists(file_path))
      required_files_[ep].second.insert(file.path);
      names.push_back(file.name);
      const auto parent = fs::path(file.name).parent_path();
      if (!parent.empty()) {
        file_subdirs_[file.path] = parent.generic_string();
      }
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
  bool success = true;
  const auto names_it = resolved_file_names_.find(ep_name);
  if (names_it == resolved_file_names_.end()) return true;
  const auto& dest_dir = required_files_[ep_name].first;
  for (const auto& file_name : names_it->second) {
    const fs::path file_path = fs::path(dest_dir) / file_name;
    if (!fs::exists(file_path)) {
      LOG4CXX_ERROR(loggerEPDependenciesManager,
                    "Unable to prepare ep dependency, name: "
                        << file_name << ", expected at: " << file_path);
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
    if (!cil::BenchmarkRunner::IsEpSupportedOnThisPlatform("", ep)) continue;
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

std::unordered_map<std::string, std::string>
EPDependenciesManager::GetFileSubdirs() const {
  return file_subdirs_;
}

}  // namespace cil
