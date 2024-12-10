#include "ep_dependencies_config.h"

#include <log4cxx/logger.h>

#include <fstream>

#include "json_schema.h"

using namespace log4cxx;

LoggerPtr loggerEPDependenciesConfig(Logger::getLogger("SystemController"));

namespace cil {

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
    std::map<std::string, std::string> collected_files;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> recursion_stack;
    bool success = CollectExtraDependencies(obj, ep_name, collected_files,
                                            visited, recursion_stack);
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
    std::map<std::string, std::string>& collected_files,
    std::unordered_set<std::string>& visited,
    std::unordered_set<std::string>& recursion_stack) {
  // Check if the EP is already in the recursion stack to avoid circular
  // dependencies
  if (recursion_stack.find(ep_name) != recursion_stack.end()) {
    LOG4CXX_ERROR(loggerEPDependenciesConfig,
                  "Circular dependency detected for EP: " << ep_name);
    return false;
  }
  // Check if the EP has already been visited to avoid duplicates
  if (visited.find(ep_name) != visited.end()) {
    return true;
  }
  // Collect all dependencies for the given EP
  if (obj.ep_dependencies_.find(ep_name) != obj.ep_dependencies_.end()) {
    recursion_stack.insert(ep_name);
    visited.insert(ep_name);
    const EPDependenciesEntryConfig& entry = obj.ep_dependencies_.at(ep_name);
    for (const auto& [file_name, file_path] : entry.GetFiles()) {
      collected_files[file_name] = file_path;
    }
    for (const auto& dep : entry.GetDependencies()) {
      if (obj.ep_dependencies_.find(dep) != obj.ep_dependencies_.end()) {
        if (!CollectExtraDependencies(obj, dep, collected_files, visited,
                                      recursion_stack))
          return false;
      } else {
        LOG4CXX_ERROR(loggerEPDependenciesConfig,
                      "EP " << dep << " not found in dependencies");
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
    std::string name = file_json.at("Name").get<std::string>();
    std::string path = file_json.at("Path").get<std::string>();
    obj.files_[name] = path;
  }
  if (j.contains("Dependencies"))
    j.at("Dependencies").get_to(obj.dependencies_);
}

}  // namespace cil
