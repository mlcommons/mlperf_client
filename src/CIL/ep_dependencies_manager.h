#ifndef EP_DEPENDENCIES_MANAGER_H_
#define EP_DEPENDENCIES_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace cil {

class Unpacker;
class EPDependenciesConfig;

class EPDependenciesManager {
 public:
  EPDependenciesManager(const std::unordered_map<std::string, std::string>& eps,
                        const std::string& config_path,
                        const std::string& config_schema_path,
                        const std::string& deps_dir);
  ~EPDependenciesManager();
  bool Initialize();

  std::unordered_set<std::string> GetRequiredFiles() const;

  bool PrepareDependenciesForEP(const std::string& ep_name);

  std::map<std::string, std::unordered_set<std::string>>
  GetEpsStorageFiles() const;


 private:
  std::unordered_map<std::string, std::string> eps_;
  std::string config_path_;
  std::string config_schema_path_;
  std::string deps_dir_;
  std::string ep_deps_dir_;

  std::unique_ptr<EPDependenciesConfig> config_;
  std::map<std::string, std::pair<std::string, std::unordered_set<std::string>>> required_files_;
};

}  // namespace cil

#endif  // !EP_DEPENDENCIES_MANAGER_H_