#ifndef SCENARIO_DATA_H
#define SCENARIO_DATA_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "execution_provider_config.h"

namespace cil {
class ScenarioDataProvider;
class FileSignatureVerifier;

struct ScenarioData {
  std::vector<std::string> model_file_paths;
  std::vector<std::string> input_file_paths;
  std::vector<std::string> asset_file_paths;
  std::optional<std::string> output_results_file_paths;

  std::vector<std::pair<ExecutionProviderConfig, std::string>> prepared_eps;

  std::map<std::string, std::string> path_to_source_map;
  std::map<std::string, std::string> source_to_path_map;
};

}  // namespace cil

#endif