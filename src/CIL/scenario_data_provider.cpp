#include "scenario_data_provider.h"

#include <log4cxx/logger.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "json_schema.h"

using namespace log4cxx;
LoggerPtr loggerDataProvider(Logger::getLogger("ScenarioDataProvider"));

namespace cil {
ScenarioDataProvider::ScenarioDataProvider(
    const std::vector<std::string>& asset_paths,
    const std::vector<std::string>& input_paths,
    const std::string& input_file_schema_path,
    const std::string& output_results_path,
    const std::string& output_results_schema_path)
    : asset_paths_(asset_paths),
      input_paths_(input_paths),
      input_file_schema_path_(input_file_schema_path),
      output_results_path_(output_results_path),
      output_results_schema_path_(output_results_schema_path) {
  ReadOutputResultsFile();
}

const std::vector<std::string>& ScenarioDataProvider::GetInputPaths() const {
  return input_paths_;
}

const std::vector<std::string>& ScenarioDataProvider::GetAssetPaths() const {
  return asset_paths_;
}

const std::string& ScenarioDataProvider::GetInputFileSchemaPath() const {
  return input_file_schema_path_;
}

void ScenarioDataProvider::ReadOutputResultsFile() {
  if (output_results_path_.empty()) {
    LOG4CXX_ERROR(loggerDataProvider, "Output results file is not provided!");
    return;
  }

  // Load the JSON data
  nlohmann::json json_data;
  std::ifstream json_file(output_results_path_);
  if (!json_file.is_open()) {
    LOG4CXX_ERROR(loggerDataProvider,
                  "Unable to load output results file with path: "
                      << output_results_path_);
    return;
  }

  try {
    json_file >> json_data;
    // Validate the JSON data against the schema
    if (!cil::ValidateJSONSchema(output_results_schema_path_, json_data)
             .empty()) {
      LOG4CXX_ERROR(loggerDataProvider,
                    "Output results file schema validation failed, path: "
                        << output_results_path_);
      return;
    }
  } catch (std::exception& e) {
    LOG4CXX_ERROR(loggerDataProvider,
                  "An exception occured while reading and validating of output "
                  "results file, path: "
                      << output_results_path_ << ", exception: " << e.what());
    return;
  }

  // For LLM generation
  if (json_data.find("expected_tokens") != json_data.end()) {
    auto& prompts_tokens = json_data.at("expected_tokens");
    for (const auto& prompt_tokens : prompts_tokens) {
      std::vector<uint32_t> tokens;
      for (const auto& token : prompt_tokens) {
        tokens.push_back(token);
      }
      expected_tokens_.push_back(tokens);
    }
  }
}

const std::vector<std::vector<uint32_t>>&
ScenarioDataProvider::GetExpectedTokens() const {
  return expected_tokens_;
}

void ScenarioDataProvider::SetLLMTokenizerPath(const std::string& path) {
  llm_tokenizer_path_ = path;
}

std::string ScenarioDataProvider::GetLLMTokenizerPath() const {
  return llm_tokenizer_path_;
}

}  // namespace cil