#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace cil {
// if the validaiton is not passed, error message will be returned, otherwise
// empty string will be returned
std::string ValidateJSONSchema(const std::string& schema_path,
                               const nlohmann::json& json_data);

// In-memory schema validation (no file I/O).
// Returns empty string on success, error description on failure.
std::string ValidateJSONSchema(const nlohmann::json& schema,
                               const nlohmann::json& json_data);

}  // namespace cil
