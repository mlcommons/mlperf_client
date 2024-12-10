#ifndef CIL_CONFIG_CREATOR_H_
#define CIL_CONFIG_CREATOR_H_

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "common/json_schema.h"
#include "execution_config.h"
#include "execution_provider_config.h"

namespace cil {

class ConfigCreator {
 public:
  ConfigCreator() = default;
  ~ConfigCreator() = default;
  bool SetSchemaFilePath(const std::string& schema_file_path);
  bool LoadFromFile(const std::string& json_file_path);
  bool SaveToFile(const std::string& json_file_path);
  bool Validate();

  const std::vector<ScenarioConfig>& GetScenarios() const { return scenarios_; }
  const SystemConfig& GetSystemConfig() const { return system_config_; }

  static void from_json(const nlohmann::json& j, ConfigCreator& obj);
  void FromJson(const nlohmann::json& j) { from_json(j, *this); }

  void AddScenario(const ScenarioConfig& scenario);
  void RemoveScenario(const std::string& scenario_name);

  void AddExecutionProvider(const std::string& scenario_name,
                            const ExecutionProviderConfig& ep);

  void RemoveExecutionProvider(const std::string& scenario_name,
                               const std::string& ep_name);

  void SetSystemConfig(const SystemConfig& system_config) {
    system_config_ = system_config;
  }

  const std::string& GetLastError() const { return last_error_; }

  nlohmann::json ToJson() const;

 private:
  std::vector<ScenarioConfig> scenarios_;
  SystemConfig system_config_;
  std::string schema_file_path_;
  std::string last_error_;
};

}  // namespace cil

#endif  // CIL_CONFIG_CREATOR_H_