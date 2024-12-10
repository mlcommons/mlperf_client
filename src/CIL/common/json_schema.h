#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace cil {
// if the validaiton is not passed, error message will be returned, otherwise
// empty string will be returned
std::string ValidateJSONSchema(const std::string& schema_path,
                               const nlohmann::json& json_data);

}  // namespace cil
