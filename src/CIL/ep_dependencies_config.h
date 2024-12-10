#ifndef EP_DEPENDENCIES_CONFIG_H_
#define EP_DEPENDENCIES_CONFIG_H_

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cil {

class EPDependenciesEntryConfig {
 public:
  EPDependenciesEntryConfig() = default;
  EPDependenciesEntryConfig(const nlohmann::json& j) { FromJson(j); }
  ~EPDependenciesEntryConfig() = default;

  const std::string& GetEPName() const { return ep_name_; }
  const std::map<std::string, std::string>& GetFiles() const { return files_; }

  static void from_json(const nlohmann::json& j,
                        EPDependenciesEntryConfig& obj);
  void FromJson(const nlohmann::json& j) { from_json(j, *this); }
  void AddExtraFiles(const std::map<std::string, std::string>& extra_files) {
    files_.insert(extra_files.begin(), extra_files.end());
  }
  const std::unordered_set<std::string>& GetDependencies() const {
    return dependencies_;
  }

 private:
  std::string ep_name_;
  std::map<std::string, std::string> files_;
  std::unordered_set<std::string> dependencies_;
};

class EPDependenciesConfig {
 public:
  EPDependenciesConfig() = default;
  ~EPDependenciesConfig() = default;

  bool ValidateAndParse(const std::string& json_file_path,
                        const std::string& schema_file_path);
  const EPDependenciesEntryConfig& GetDependenciesForEP(
      const std::string& ep_name) {
    return ep_dependencies_[ep_name];
  }

  static void from_json(const nlohmann::json& j, EPDependenciesConfig& obj);
  void FromJson(const nlohmann::json& j) { from_json(j, *this); }
  bool AreDependenciesResolved() const { return dependencies_resolved_; }

 private:
  std::map<std::string, EPDependenciesEntryConfig> ep_dependencies_;
  bool dependencies_resolved_ = false;

  static bool CollectExtraDependencies(
      const EPDependenciesConfig& obj, const std::string& ep_name,
      std::map<std::string, std::string>& collected_files,
      std::unordered_set<std::string>& visited,
      std::unordered_set<std::string>& recursion_stack);
};

}  // namespace cil

#endif  // EP_DEPENDENCIES_CONFIG_H_
