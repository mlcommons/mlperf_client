#include "ep_dependencies_config.h"

#include <log4cxx/logger.h>

#include <algorithm>
#include <fstream>

#include "json_schema.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerEPDependenciesConfig(Logger::getLogger("SystemController"));

namespace cil {

namespace {

// Case-insensitive match of a JSON scalar (string/number/bool) vs allowed values.
bool JsonValueMatchesAllowed(const nlohmann::json& value,
                             const std::vector<std::string>& allowed) {
  std::string actual;
  if (value.is_string()) {
    actual = value.get<std::string>();
  } else if (value.is_number_integer()) {
    actual = std::to_string(value.get<long long>());
  } else if (value.is_number_float()) {
    actual = std::to_string(value.get<double>());
  } else if (value.is_boolean()) {
    actual = value.get<bool>() ? "true" : "false";
  } else {
    return false;
  }
  const std::string actual_lc = utils::StringToLowerCase(actual);
  for (const auto& candidate : allowed) {
    if (utils::StringToLowerCase(candidate) == actual_lc) return true;
  }
  return false;
}

bool ClauseMatchesConfig(const EPDependencyConditionClause& clause,
                         const nlohmann::json& ep_config) {
  for (const auto& [key, allowed_values] : clause) {
    if (!ep_config.is_object() || !ep_config.contains(key)) return false;
    if (!JsonValueMatchesAllowed(ep_config.at(key), allowed_values))
      return false;
  }
  return true;
}

bool ConditionMatchesConfig(const EPDependencyCondition& condition,
                            const nlohmann::json& ep_config) {
  if (condition.empty()) return true;
  for (const auto& clause : condition) {
    if (ClauseMatchesConfig(clause, ep_config)) return true;
  }
  return false;
}

// Parse a single AND-clause object: { key: "v" | ["v1","v2"], ... }.
EPDependencyConditionClause ParseConditionClause(const nlohmann::json& obj) {
  EPDependencyConditionClause clause;
  if (!obj.is_object()) return clause;
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    std::vector<std::string> values;
    if (it.value().is_string()) {
      values.push_back(it.value().get<std::string>());
    } else if (it.value().is_array()) {
      for (const auto& v : it.value())
        if (v.is_string()) values.push_back(v.get<std::string>());
    }
    if (!values.empty()) clause[it.key()] = std::move(values);
  }
  return clause;
}

// Parse a Condition: an object (one clause) or array of objects (OR of clauses).
EPDependencyCondition ParseCondition(const nlohmann::json& cond_json) {
  EPDependencyCondition condition;
  auto add = [&](const nlohmann::json& clause_json) {
    auto clause = ParseConditionClause(clause_json);
    if (!clause.empty()) condition.push_back(std::move(clause));
  };
  if (cond_json.is_array()) {
    for (const auto& clause_json : cond_json) add(clause_json);
  } else if (cond_json.is_object()) {
    add(cond_json);
  }
  return condition;
}

// AND two OR-conditions: empty => the other; else merge each clause pair
// (key union, intersect allowed values on collision, drop if intersection empty).
EPDependencyCondition CombineConditions(const EPDependencyCondition& a,
                                        const EPDependencyCondition& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  EPDependencyCondition out;
  for (const auto& ca : a) {
    for (const auto& cb : b) {
      EPDependencyConditionClause merged = ca;
      bool drop = false;
      for (const auto& [key, b_vals] : cb) {
        auto it = merged.find(key);
        if (it == merged.end()) {
          merged[key] = b_vals;
          continue;
        }
        std::vector<std::string> inter;
        for (const auto& v : it->second)
          if (std::find(b_vals.begin(), b_vals.end(), v) != b_vals.end())
            inter.push_back(v);
        if (inter.empty()) {
          drop = true;
          break;
        }
        it->second = std::move(inter);
      }
      if (!drop) out.push_back(std::move(merged));
    }
  }
  return out;
}

}  // namespace

std::vector<EPDependencyFile> EPDependenciesEntryConfig::FilterFiles(
    const std::vector<nlohmann::json>& ep_configs) const {
  std::vector<EPDependencyFile> out;
  out.reserve(files_.size());
  for (const auto& f : files_) {
    if (f.condition.empty()) {
      out.push_back(f);
      continue;
    }
    for (const auto& cfg : ep_configs) {
      if (ConditionMatchesConfig(f.condition, cfg)) {
        out.push_back(f);
        break;
      }
    }
  }
  return out;
}

void EPDependenciesEntryConfig::AddExtraFiles(
    const std::vector<EPDependencyFile>& extra_files) {
  // De-dup by Name; an EP's own file wins over a depended-on EP's.
  for (const auto& extra : extra_files) {
    const auto it = std::find_if(
        files_.begin(), files_.end(),
        [&](const EPDependencyFile& f) { return f.name == extra.name; });
    if (it == files_.end()) {
      files_.push_back(extra);
    }
  }
}

bool EPDependenciesConfig::ValidateAndParse(
    const std::string& json_file_path, const std::string& schema_file_path) {
  // Load the JSON data
  nlohmann::json json_data;
  std::ifstream json_file(json_file_path);
  if (!json_file.is_open()) {
    LOG4CXX_ERROR(
        loggerEPDependenciesConfig,
        "Unable to load Ep dependencies file with path: " << json_file_path);
    return false;
  }

  try {
    json_file >> json_data;
    // Validate the JSON data against the schema
    if (!ValidateJSONSchema(schema_file_path, json_data).empty()) return false;
  } catch (...) {
    return false;
  }
  FromJson(json_data);

  return true;
}

void EPDependenciesConfig::from_json(const nlohmann::json& j,
                                     EPDependenciesConfig& obj) {
  const auto& ep_dependencies_json = j.at("EPDependencies");
  for (const auto& json_entry : ep_dependencies_json) {
    EPDependenciesEntryConfig entry;
    entry.FromJson(json_entry);
    obj.ep_dependencies_[entry.GetEPName()] = entry;
  }
  obj.dependencies_resolved_ = true;
  for (auto& [ep_name, entry] : obj.ep_dependencies_) {
    // Resolve dependencies
    // Each EP can have dependencies on other EPs
    std::vector<EPDependencyFile> collected_files;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> recursion_stack;
    bool success = CollectExtraDependencies(obj, ep_name, /*inherited=*/{},
                                            collected_files, visited,
                                            recursion_stack);
    if (!success) {
      LOG4CXX_ERROR(loggerEPDependenciesConfig,
                    "Failed to collect dependencies for EP: " << ep_name);
      obj.dependencies_resolved_ = false;
      return;
    }
    entry.AddExtraFiles(collected_files);
  }
}
bool EPDependenciesConfig::CollectExtraDependencies(
    const EPDependenciesConfig& obj, const std::string& ep_name,
    const EPDependencyCondition& inherited_condition,
    std::vector<EPDependencyFile>& collected_files,
    std::unordered_set<std::string>& visited,
    std::unordered_set<std::string>& recursion_stack) {
  if (recursion_stack.find(ep_name) != recursion_stack.end()) {
    LOG4CXX_ERROR(loggerEPDependenciesConfig,
                  "Circular dependency detected for EP: " << ep_name);
    return false;
  }
  if (visited.find(ep_name) != visited.end()) {
    return true;
  }
  if (obj.ep_dependencies_.find(ep_name) != obj.ep_dependencies_.end()) {
    recursion_stack.insert(ep_name);
    visited.insert(ep_name);
    const EPDependenciesEntryConfig& entry = obj.ep_dependencies_.at(ep_name);
    for (const auto& f : entry.GetAllFiles()) {
      const auto it =
          std::find_if(collected_files.begin(), collected_files.end(),
                       [&](const EPDependencyFile& existing) {
                         return existing.name == f.name;
                       });
      if (it == collected_files.end()) {
        EPDependencyFile gated = f;
        gated.condition = CombineConditions(inherited_condition, f.condition);
        collected_files.push_back(std::move(gated));
      }
    }
    for (const auto& dep : entry.GetDependencies()) {
      if (obj.ep_dependencies_.find(dep.name) != obj.ep_dependencies_.end()) {
        const EPDependencyCondition edge_condition =
            CombineConditions(inherited_condition, dep.condition);
        if (!CollectExtraDependencies(obj, dep.name, edge_condition,
                                      collected_files, visited,
                                      recursion_stack))
          return false;
      } else {
        LOG4CXX_ERROR(loggerEPDependenciesConfig,
                      "EP " << dep.name << " not found in dependencies");
        return false;
      }
    }
    recursion_stack.erase(ep_name);
    return true;
  } else {
    LOG4CXX_ERROR(loggerEPDependenciesConfig,
                  "EP " << ep_name << " not found in the dependencies");
    return false;
  }
}

void EPDependenciesEntryConfig::from_json(const nlohmann::json& j,
                                          EPDependenciesEntryConfig& obj) {
  j.at("Name").get_to(obj.ep_name_);

  const auto& files_json = j.at("Files");
  for (const auto& file_json : files_json) {
    EPDependencyFile f;
    f.name = file_json.at("Name").get<std::string>();
    f.path = file_json.at("Path").get<std::string>();
    if (file_json.contains("Condition"))
      f.condition = ParseCondition(file_json.at("Condition"));
    obj.files_.push_back(std::move(f));
  }
  // Dependency item: plain name (string) or { Name, Condition } for a gated edge.
  if (j.contains("Dependencies")) {
    for (const auto& dep_json : j.at("Dependencies")) {
      EPDependencyRef ref;
      if (dep_json.is_string()) {
        ref.name = dep_json.get<std::string>();
      } else if (dep_json.is_object()) {
        ref.name = dep_json.at("Name").get<std::string>();
        if (dep_json.contains("Condition"))
          ref.condition = ParseCondition(dep_json.at("Condition"));
      }
      if (!ref.name.empty()) obj.dependencies_.push_back(std::move(ref));
    }
  }
}

}  // namespace cil
