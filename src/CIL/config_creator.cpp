#include "config_creator.h"

#include <log4cxx/logger.h>

#include <iostream>

#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerConfigCreator(Logger::getLogger("ConfigCreator"));

namespace cil {

bool ConfigCreator::SetSchemaFilePath(const std::string& schema_file_path) {
  last_error_.clear();
  if (!utils::FileExists(schema_file_path)) {
    LOG4CXX_ERROR(loggerConfigCreator,
                  "Schema file " << schema_file_path << " does not exist");
    last_error_ = "Schema file does not exist";
    return false;
  }
  schema_file_path_ = schema_file_path;
  return true;
}
bool ConfigCreator::LoadFromFile(const std::string& json_file_path) {
  last_error_.clear();
  nlohmann::json json_data;
  std::ifstream json_file(json_file_path);
  if (!json_file.is_open()) {
    LOG4CXX_ERROR(loggerConfigCreator, "Failed to open " << json_file_path);
    last_error_ = "Failed to open file";
    return false;
  }
  try {
    json_file >> json_data;
    std::string error_string =
        cil::ValidateJSONSchema(schema_file_path_, json_data);
    if (!error_string.empty()) {
      LOG4CXX_ERROR(loggerConfigCreator, "Failed to validate config file "
                                             << json_file_path << ", "
                                             << error_string);
      last_error_ = "Failed to validate config file";
      return false;
    }

  } catch (...) {
    LOG4CXX_ERROR(loggerConfigCreator, "Failed to parse " << json_file_path);
    last_error_ = "Failed to parse file";
    return false;
  }
  FromJson(json_data);
  return true;
}

bool ConfigCreator::SaveToFile(const std::string& json_file_path) {
  last_error_.clear();
  bool valid = Validate();
  if (!valid) {
    return false;
  }
  nlohmann::json j = ToJson();
  std::ofstream json_file(json_file_path);
  if (!json_file.is_open()) {
    LOG4CXX_ERROR(loggerConfigCreator, "Failed to save to " << json_file_path);
    last_error_ = "Failed to save to file to: " + json_file_path;
    return false;
  }
  json_file << j.dump(4);
  return true;
}

void ConfigCreator::AddScenario(const ScenarioConfig& scenario) {
  scenarios_.emplace_back(scenario);
}

void ConfigCreator::RemoveScenario(const std::string& scenario_name) {
  scenarios_.erase(
      std::remove_if(scenarios_.begin(), scenarios_.end(),
                     [&scenario_name](const ScenarioConfig& scenario) {
                       return scenario.GetName() == scenario_name;
                     }),
      scenarios_.end());
}

void ConfigCreator::AddExecutionProvider(const std::string& scenario_name,
                                         const ExecutionProviderConfig& ep) {
  auto scenario =
      std::find_if(scenarios_.begin(), scenarios_.end(),
                   [&scenario_name](const ScenarioConfig& scenario) {
                     return scenario.GetName() == scenario_name;
                   });
  if (scenario != scenarios_.end()) {
    scenario->AddExecutionProvider(ep);
  }
}

void ConfigCreator::RemoveExecutionProvider(const std::string& scenario_name,
                                            const std::string& ep_name) {
  auto scenario =
      std::find_if(scenarios_.begin(), scenarios_.end(),
                   [&scenario_name](const ScenarioConfig& scenario) {
                     return scenario.GetName() == scenario_name;
                   });
  if (scenario != scenarios_.end()) {
    scenario->RemoveExecutionProvider(ep_name);
  }
}

void ConfigCreator::from_json(const nlohmann::json& j, ConfigCreator& obj) {
  const auto system_json = j.at("SystemConfig");
  obj.system_config_.FromJson(system_json);

  const auto& scenarios_json = j.at("Scenarios");
  for (const auto& scenario_json : scenarios_json) {
    ScenarioConfig scenario_config;
    scenario_config.FromJson(scenario_json);
    obj.scenarios_.emplace_back(scenario_config);
  }
}

nlohmann::json ConfigCreator::ToJson() const {
  nlohmann::json j;
  j["SystemConfig"] = system_config_.ToJson();
  j["Scenarios"] = nlohmann::json::array();
  for (const auto& scenario : scenarios_) {
    j["Scenarios"].push_back(scenario.ToJson());
  }
  return j;
}

bool ConfigCreator::Validate() {
  last_error_.clear();
  nlohmann::json j = ToJson();
  if (j.empty()) {
    LOG4CXX_ERROR(loggerConfigCreator, "Failed to create JSON object.");
    last_error_ = "Failed to create JSON object";
    return false;
  }
  std::string error_string = cil::ValidateJSONSchema(schema_file_path_, j);
  if (!error_string.empty()) {
    LOG4CXX_ERROR(loggerConfigCreator,
                  "Json object is not valid as per schema, " << error_string);
    last_error_ = "Json object is not valid as per schema";
    return false;
  }
  return true;
}

}  // namespace cil