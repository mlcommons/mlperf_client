#ifndef EP_DEPENDENCIES_CONFIG_H_
#define EP_DEPENDENCIES_CONFIG_H_

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cil {

// One AND-clause of a Condition: every key must match (a key's values are OR'd).
using EPDependencyConditionClause =
    std::map<std::string, std::vector<std::string>>;

// A Condition: OR of clauses, gating on backend and/or vendor. Empty => always.
using EPDependencyCondition = std::vector<EPDependencyConditionClause>;

// A file in an EP dependency group, kept only if its Condition matches.
struct EPDependencyFile {
  std::string name;
  std::string path;
  EPDependencyCondition condition;  // empty => unconditional
};

// A dependency edge on another EP; pulled files inherit this Condition.
struct EPDependencyRef {
  std::string name;
  EPDependencyCondition condition;  // empty => unconditional edge
};

class EPDependenciesEntryConfig {
 public:
  EPDependenciesEntryConfig() = default;
  EPDependenciesEntryConfig(const nlohmann::json& j) { FromJson(j); }
  ~EPDependenciesEntryConfig() = default;

  const std::string& GetEPName() const { return ep_name_; }

  // Every file (conditional or not); the asset packager embeds them all.
  const std::vector<EPDependencyFile>& GetAllFiles() const { return files_; }

  // Files whose Condition matches at least one of `ep_configs` (empty => kept).
  std::vector<EPDependencyFile> FilterFiles(
      const std::vector<nlohmann::json>& ep_configs) const;

  static void from_json(const nlohmann::json& j,
                        EPDependenciesEntryConfig& obj);
  void FromJson(const nlohmann::json& j) { from_json(j, *this); }
  void AddExtraFiles(const std::vector<EPDependencyFile>& extra_files);
  const std::vector<EPDependencyRef>& GetDependencies() const {
    return dependencies_;
  }

 private:
  std::string ep_name_;
  std::vector<EPDependencyFile> files_;
  std::vector<EPDependencyRef> dependencies_;
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

  // inherited_condition is AND-ed onto every collected file; callers pass empty.
  static bool CollectExtraDependencies(
      const EPDependenciesConfig& obj, const std::string& ep_name,
      const EPDependencyCondition& inherited_condition,
      std::vector<EPDependencyFile>& collected_files,
      std::unordered_set<std::string>& visited,
      std::unordered_set<std::string>& recursion_stack);
};

}  // namespace cil

#endif  // EP_DEPENDENCIES_CONFIG_H_
