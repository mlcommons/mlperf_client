#include "json_schema.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json-schema.hpp>
#include <regex>

std::string cil::ValidateJSONSchema(const std::string& schema_path,
                                    const nlohmann::json& json_data) {
  nlohmann::json schema_json;
  std::ifstream schema_file(schema_path);
  if (!schema_file.is_open())
    return "Failed to open the schema file: " + schema_path;
  schema_file >> schema_json;

  // Create a JSON schema validator
  nlohmann::json_schema::json_validator validator;
  try {
    validator.set_root_schema(schema_json);
  } catch (const std::exception& e) {
    return "Setting schema failed: " + std::string(e.what());
  }

  // Validate the JSON data
  try {
    validator.validate(json_data);
  } catch (const std::exception& e) {
    // check if error happened because of pattern mismatch for the path
    if (std::string(e.what()).find("pattern") != std::string::npos) {
      // check if error related to paths in the scenarios we can validate
      // that by checking if the data invalid at /Scenarios/index/path
      std::regex pattern(R"(/Scenarios/(\d+)/(\w+)/(\d+))");
      std::string error = e.what();
      std::smatch matches;
      if (std::regex_search(error, matches, pattern)) {
        return "Invalid path in scenario number: " + matches[1].str() + " " +
               matches[2].str() +
               " paths should follow the schema [file://path_to_file for "
               "local files or http:// or https:// web based files]";
      } else {
        return "JSON data is invalid: " + std::string(e.what());
      }

    } else {
      return "JSON data is invalid: " + std::string(e.what());
    }

    return std::string();
  }

  return std::string();
}
