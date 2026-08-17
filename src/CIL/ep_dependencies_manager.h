#ifndef EP_DEPENDENCIES_MANAGER_H_
#define EP_DEPENDENCIES_MANAGER_H_

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cil {

class Unpacker;
class EPDependenciesConfig;

class EPDependenciesManager {
 public:
  // `eps` maps EP-name -> destination directory.
  // `ep_configs` (optional) maps EP-name -> the list of scenario configs that
  // referenced that EP. Used to evaluate per-file Conditions in the JSON
  // dependency config (e.g. an NV-only DLL gated on
  // `"Condition": { "device_ep": "NvTensorRtRtx" }`). If an EP has no entry
  // here, conditional files for it are dropped.
  EPDependenciesManager(
      const std::unordered_map<std::string, std::string>& eps,
      const std::string& config_path, const std::string& config_schema_path,
      const std::string& deps_dir,
      const std::unordered_map<std::string, std::vector<nlohmann::json>>&
          ep_configs = {});
  ~EPDependenciesManager();
  bool Initialize();

  std::unordered_set<std::string> GetRequiredFiles() const;

  bool PrepareDependenciesForEP(const std::string& ep_name);

  std::map<std::string, std::unordered_set<std::string>> GetEpsStorageFiles()
      const;

  /// Returns URL -> sub-directory mappings for files whose `Name` in the
  /// deps JSON contains path separators (e.g. "tensorrtrtx/foo.dll"). The
  /// download stage threads these into Storage's `sub_dir` argument so the
  /// downloaded file lands at `<dest>/<sub_dir>/<filename>`.
  std::unordered_map<std::string, std::string> GetFileSubdirs() const;

 private:
  std::unordered_map<std::string, std::string> eps_;
  std::string config_path_;
  std::string config_schema_path_;
  std::string deps_dir_;
  std::string ep_deps_dir_;
  std::unordered_map<std::string, std::vector<nlohmann::json>> ep_configs_;

  std::unique_ptr<EPDependenciesConfig> config_;
  std::map<std::string, std::pair<std::string, std::unordered_set<std::string>>>
      required_files_;
  // Tracks the resolved file-name -> dest-dir for each EP so
  // PrepareDependenciesForEP can verify the same set Initialize selected.
  std::map<std::string, std::vector<std::string>> resolved_file_names_;
  /// URL -> sub-directory derived from file.name's parent path. See
  /// GetFileSubdirs().
  std::unordered_map<std::string, std::string> file_subdirs_;
};

}  // namespace cil

#endif  // !EP_DEPENDENCIES_MANAGER_H_
