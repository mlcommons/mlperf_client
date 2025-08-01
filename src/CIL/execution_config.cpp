#include "execution_config.h"

#include <log4cxx/logger.h>

#include <fstream>
#include <iostream>

#include "execution_provider.h"
#include "execution_provider_config.h"
#include "json_schema.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerExecutionConfig(Logger::getLogger("ExecutionConfig"));

namespace cil {

bool ExecutionConfig::ValidateAndParse(
    const std::string& json_file_path, const std::string& schema_file_path,
    const std::string& config_verification_file_path,
    const std::string& config_experimental_verification_file_path) {
  config_verified_ = false;
  config_experimental_ = false;
  // Load the JSON data
  nlohmann::json json_data;
  std::ifstream json_file(json_file_path);
  if (!json_file.is_open()) {
    LOG4CXX_ERROR(loggerExecutionConfig, "Failed to open " << json_file_path);
    return false;
  }

  try {
    json_file >> json_data;
    // Validate the JSON data against the schema
    std::string error_string =
        cil::ValidateJSONSchema(schema_file_path, json_data);
    if (!error_string.empty()) {
      LOG4CXX_ERROR(loggerExecutionConfig, "Failed to validate config file "
                                               << json_file_path << ", "
                                               << error_string);
      return false;
    }

  } catch (const std::exception& e) {
    std::string error =
        "Failed to parse JSON file: " + json_file_path + ", " + e.what();
    LOG4CXX_ERROR(loggerExecutionConfig, error);
    return false;
  }
  FromJson(json_data);

  config_file_name_ = fs::path(json_file_path).filename().string();
  if (config_verification_file_path.empty() &&
      config_experimental_verification_file_path.empty())
    return true;

  config_file_hash_ =
      utils::ComputeFileSHA256(json_file_path, loggerExecutionConfig);
  if (config_file_hash_.empty()) {
    LOG4CXX_ERROR(loggerExecutionConfig,
                  "Failed to compute the hash of the config file");
    return true;
  }

  if (!config_verification_file_path.empty())
    config_verified_ = VerifyConfigFileHash(
        config_verification_file_path, config_file_hash_, config_file_name_);
  if (!config_experimental_verification_file_path.empty())
    config_experimental_ =
        VerifyConfigFileHash(config_experimental_verification_file_path,
                             config_file_hash_, config_file_name_);

  return true;
}

void ExecutionConfig::from_json(const nlohmann::json& j, ExecutionConfig& obj) {
  const auto system_json = j.at("SystemConfig");
  obj.system_config_.FromJson(system_json);

  const auto& scenarios_json = j.at("Scenarios");
  for (const auto& scenario_json : scenarios_json) {
    ScenarioConfig scenario_config;
    scenario_config.FromJson(scenario_json, obj.system_config_.GetBaseDir());
    obj.scenarios_.emplace_back(scenario_config);
  }
}

bool ExecutionConfig::VerifyConfigFileHash(
    std::string_view verification_file_path, std::string_view hash,
    std::string& file_name) const {
  nlohmann::json verification_json;
  std::ifstream verification_file{std::string(verification_file_path)};

  if (!verification_file.is_open()) {
    LOG4CXX_ERROR(
        loggerExecutionConfig,
        "Failed to open config verification file: " << verification_file_path);
    return true;
  }

  try {
    verification_file >> verification_json;
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(
        loggerExecutionConfig,
        "Failed to parse config verification file: " << verification_file_path);
    return true;
  }

  for (const auto& [key, value] : verification_json.items())
    if (key == hash) {
      file_name = value;
      return true;
    }

  return false;
}

void SystemConfig::from_json(const nlohmann::json& j, SystemConfig& obj) {
  // DownloadBehavior is optional, so we have to check if it exists
  if (j.contains("DownloadBehavior")) {
    j.at("DownloadBehavior").get_to(obj.download_behavior_);
  }
  if (j.contains("CacheLocalFiles")) {
    j.at("CacheLocalFiles").get_to(obj.cache_local_files_);
  }
  // comment is optional, so we have to check if it exists
  if (j.contains("Comment")) {
    j.at("Comment").get_to(obj.comment_);
  }
  std::string tempPath = j.value("TempPath", "");
  if (tempPath.empty()) {
    obj.temp_path_ = "";
    obj.is_temp_path_correct_ = true;
  } else {
    std::string original_temp_path = tempPath;
    // Remove file scheme if it exists in the path, file starts with file://
    if (tempPath.find("file://") == 0) {
      tempPath = tempPath.substr(7);
    }
    // Normalize the path
    tempPath = utils::NormalizePath(tempPath);
    // Check if there's any illegal characters in the path
    if (!utils::IsValidPath(tempPath)) {
      LOG4CXX_ERROR(
          loggerExecutionConfig,
          "TempPath contains illegal characters: " << original_temp_path);
    } else {
      std::filesystem::path p = tempPath;
      if (p.is_relative()) {
        p = std::filesystem::absolute(p);
      }
      obj.temp_path_ = p.string();
      obj.is_temp_path_correct_ = true;
    }
  }
  if (j.contains("EPDependenciesConfigPath")) {
    j.at("EPDependenciesConfigPath").get_to(obj.ep_dependencies_config_path_);
  }
  // Validate the base directory
  if (j.contains("BaseDir")) {
    std::string base_dir = j.value("BaseDir", "");
    if (base_dir.find("file://") == 0) {
      base_dir = base_dir.substr(7);
      // Base directory should be a existing directory
      auto base_dir_path = fs::path(base_dir);
      if (!fs::exists(base_dir_path) || !fs::is_directory(base_dir_path)) {
        LOG4CXX_ERROR(loggerExecutionConfig,
                      "BaseDir is not a valid directory: " << base_dir);
      } else {
        obj.base_dir_ = base_dir;
        obj.is_base_dir_correct_ = true;
      }
    } else {
      LOG4CXX_ERROR(loggerExecutionConfig,
                    "BaseDir should start with file://: " << base_dir);
    }
  } else {
    obj.base_dir_ = "";
    obj.is_base_dir_correct_ = true;
  }
}

ScenarioConfig::ModelsMap ScenarioConfig::GetModelFiles() const {
  ModelsMap paths;
  auto retreivePathsFromModel = [&paths](const ModelConfig& model) {
    std::string model_key = model.GetFilePath() + "_" +
                            model.GetDataFilePath() + "_" +
                            model.GetTokenizerPath();

    if (auto& file_path = model.GetFilePath();
        !model.GetFilePath().empty() &&
        std::find_if(paths.begin(), paths.end(),
                     [&file_path](const auto& path) {
                       return path.first == file_path;
                     }) == paths.end())
      paths.push_back({model.GetFilePath(), model_key});
  };

  auto retrievePathsFromExecutionProvider =
      [&retreivePathsFromModel](const ExecutionProviderConfig& ep) {
#if IGNORE_FILES_FROM_DISABLED_EPS && 1
        if (ep.GetLibraryPath().empty() &&
            !cil::utils::IsEpSupportedOnThisPlatform("", ep.GetName()))
          return;
#endif
        for (const auto& model : ep.GetModels()) retreivePathsFromModel(model);
      };

  for (const auto& model : models_) retreivePathsFromModel(model);
  for (const auto& ep : execution_providers_)
    retrievePathsFromExecutionProvider(ep);
  return paths;
}

std::unordered_map<std::string, std::string>
ScenarioConfig::GetModelExtraFiles() const {
  std::unordered_map<std::string, std::string> paths;
  auto retreivePathsFromModel = [&paths](const ModelConfig& model) {
    std::string model_key = model.GetFilePath() + "_" +
                            model.GetDataFilePath() + "_" +
                            model.GetTokenizerPath();
    if (!model.GetDataFilePath().empty())
      paths[model.GetDataFilePath()] = model_key;

    if (!model.GetTokenizerPath().empty())
      paths[model.GetTokenizerPath()] = model_key;

    for (const auto& path : model.GetAdditionalPaths()) paths[path] = model_key;
  };

  auto retrievePathsFromExecutionProvider =
      [&retreivePathsFromModel](const ExecutionProviderConfig& ep) {
#if IGNORE_FILES_FROM_DISABLED_EPS && 1
        if (ep.GetLibraryPath().empty() &&
            !cil::utils::IsEpSupportedOnThisPlatform("", ep.GetName()))
          return;
#endif
        for (const auto& model : ep.GetModels()) retreivePathsFromModel(model);
      };

  for (const auto& model : models_) retreivePathsFromModel(model);
  for (const auto& ep : execution_providers_)
    retrievePathsFromExecutionProvider(ep);

  return paths;
}

ModelConfig ScenarioConfig::GetModelByFilePath(
    const std::string& file_path) const {
  for (const auto& model : models_)
    if (model.GetFilePath() == file_path) return model;
  return ModelConfig();
}

const std::map<std::string, std::vector<std::string>>
ScenarioConfig::GetExecutionProvidersDependencies() const {
  std::map<std::string, std::vector<std::string>> dependencies_map;
  for (const auto& ep : execution_providers_) {
    std::vector<std::string> dependencies;
    for (const auto& dep : ep.GetDependencies()) {
      dependencies.push_back(dep);
    }
    if (!dependencies.empty()) dependencies_map[ep.GetName()] = dependencies;
  }
  return dependencies_map;
}

void ScenarioConfig::from_json(const nlohmann::json& j, ScenarioConfig& obj,
                               const std::string& base_dir) {
  j.at("Name").get_to(obj.name_);
  obj.name_ = utils::StringToLowerCase(obj.name_);

  const auto& models_json = j.at("Models");
  for (const auto& model_json : models_json) {
    ModelConfig model;
    model.FromJson(model_json, base_dir);
    auto lower_case_model_name = utils::StringToLowerCase(obj.name_);
    if (lower_case_model_name == "llama2" ||
        lower_case_model_name == "llama3" ||
        lower_case_model_name == "phi3.5" ||
        lower_case_model_name == "phi4") {
      // tokenizer path is required for the LLMs
      if (model.GetTokenizerPath().empty())
        LOG4CXX_ERROR(
            loggerExecutionConfig,
            "Configuration for the " + lower_case_model_name + " model "
                << model.GetName()
                << " is not valid, make sure to provide TokenizerPath");
    }
    obj.models_.emplace_back(model);
  }
  j.at("InputFilePath").get_to(obj.inputs_);
  j.at("ResultsVerificationFile").get_to(obj.results_file_);
  j.at("DataVerificationFile").get_to(obj.data_verification_file_);
  j.at("Iterations").get_to(obj.iterations_);
  if (j.contains("WarmUp")) {
    j.at("WarmUp").get_to(obj.iterations_warmup_);
  } else {
    obj.iterations_warmup_ = 1;
  }
  if (j.contains("Delay")) {
    j.at("Delay").get_to(obj.inference_delay_);
  } else {
    obj.inference_delay_ =
        obj.name_ == "llama2" || obj.name_ == "llama3" || obj.name_ == "phi3.5" || obj.name_ == "phi4"
            ? 5.0
            : 0.0;  // seconds
  }
  if (j.contains("AssetsPath")) j.at("AssetsPath").get_to(obj.assets_);

  const auto& execution_providers_json = j.at("ExecutionProviders");
  for (const auto& ep_json : execution_providers_json) {
    ExecutionProviderConfig epc;
    epc.FromJson(ep_json, base_dir);
    obj.execution_providers_.emplace_back(epc);
  }

  // Prepend base directory for relative paths, if needed

  for (auto& input : obj.inputs_) {
    input = utils::PrependBaseDirIfNeeded(input, base_dir);
  }

  obj.results_file_ =
      utils::PrependBaseDirIfNeeded(obj.results_file_, base_dir);

  obj.data_verification_file_ =
      utils::PrependBaseDirIfNeeded(obj.data_verification_file_, base_dir);

  for (auto& asset : obj.assets_) {
    asset = utils::PrependBaseDirIfNeeded(asset, base_dir);
  }
}

nlohmann::json ScenarioConfig::ToJson() const {
  nlohmann::json j;
  j["Name"] = name_;
  j["InputFilePath"] = inputs_;
  j["AssetsPath"] = assets_;
  j["ResultsVerificationFile"] = results_file_;
  j["DataVerificationFile"] = data_verification_file_;
  j["Iterations"] = iterations_;
  // we need to serialize the models and execution providers
  j["Models"] = nlohmann::json::array();
  for (const auto& model : models_) {
    j["Models"].push_back(model.ToJson());
  }
  j["ExecutionProviders"] = nlohmann::json::array();
  for (const auto& ep : execution_providers_) {
    j["ExecutionProviders"].push_back(ep.ToJson());
  }
  return j;
}

bool ExecutionConfig::isConfigFileValid(const std::string& json_path) {
  bool isValid = true;
  if (json_path.empty()) {
    isValid = false;
  } else {
    fs::path json_file_path(json_path);
    if (!fs::exists(json_file_path) || !fs::is_regular_file(json_file_path)) {
      isValid = false;
    }
    // check if the file is a json file
    if (isValid && json_file_path.extension() != ".json") {
      isValid = false;
    }
  }
  return isValid;
}

std::filesystem::path GetExecutionProviderParentLocation(
    const cil::ExecutionProviderConfig& ep_config,
    const std::string& deps_dir) {
  const auto& ep_name = ep_config.GetName();

  auto library_path = ep_config.GetLibraryPath();

  // create path variable to store the destination directory
  fs::path dest_dir;
  if (!library_path.empty()) {
    // Remove the file:// prefix if it exists
    if (library_path.find("file://") == 0) {
      library_path = library_path.substr(7);
    }
    dest_dir = fs::path(library_path).parent_path();
  } else {
    dest_dir = fs::path(deps_dir) / cil::GetEPDependencySubdirPath(ep_name);
  }
  return dest_dir;
}

nlohmann::json ExecutionConfig::GetEPsConfigSchema(
    const std::string& schema_file_path) {
  nlohmann::json output = nlohmann::json::object();
  try {
    std::ifstream file(schema_file_path);
    if (!file.is_open()) {
      LOG4CXX_ERROR(loggerExecutionConfig,
                    "Failed to open the schema file: " << schema_file_path);
      return output;
    }
    nlohmann::json schema;
    file >> schema;
    if (!(schema.contains("properties") &&
          schema["properties"].contains("Scenarios"))) {
      LOG4CXX_ERROR(loggerExecutionConfig,
                    "'Scenarios' field not found: " << schema_file_path);
      return output;
    }
    const auto& scenarios = schema["properties"]["Scenarios"];
    if (!(scenarios.contains("items") &&
          scenarios["items"].contains("properties") &&
          scenarios["items"]["properties"].contains("ExecutionProviders"))) {
      LOG4CXX_ERROR(
          loggerExecutionConfig,
          "'ExecutionProviders' field not found: " << schema_file_path);
      return output;
    }
    const auto& execution_providers_schema =
        scenarios["items"]["properties"]["ExecutionProviders"];
    if (!(execution_providers_schema.contains("type") &&
          execution_providers_schema["type"] == "array")) {
      LOG4CXX_ERROR(
          loggerExecutionConfig,
          "'ExecutionProviders' field is not an array: " << schema_file_path);
      return output;
    }
    if (!execution_providers_schema.contains("items") ||
        !execution_providers_schema["items"].contains("properties")) {
      return output;
    }

    const auto& item_properties =
        execution_providers_schema["items"]["properties"];

    if (!execution_providers_schema["items"].contains("allOf")) {
      return output;
    }

    for (const auto& condition : execution_providers_schema["items"]["allOf"]) {
      if (!condition.contains("if") || !condition.contains("then")) {
        continue;
      }
      if (condition["if"].contains("properties") &&
          condition["if"]["properties"].contains("Name")) {
        std::string name_value =
            condition["if"]["properties"]["Name"].value("const", "");
        nlohmann::json fields = nlohmann::json::object();

        if (condition["then"].contains("properties") &&
            condition["then"]["properties"].contains("Config") &&
            condition["then"]["properties"]["Config"].contains("properties")) {
          for (const auto& [prop_name, prop_schema] :
               condition["then"]["properties"]["Config"]["properties"]
                   .items()) {
            fields[prop_name] = prop_schema;
          }
        }
        output[name_value] = fields;
      }
    }
  } catch (const std::exception& e) {
    LOG4CXX_ERROR(loggerExecutionConfig,
                  "Error parsing schema file: " << schema_file_path);
  }

  return output;
}

}  // namespace cil