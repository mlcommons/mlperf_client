#ifndef SCENARIO_DATA_PROVIDER_H_
#define SCENARIO_DATA_PROVIDER_H_

#include <string>
#include <vector>

namespace cil {

class ScenarioDataProvider {
 public:
  ScenarioDataProvider(const std::vector<std::string>& asset_paths,
                       const std::vector<std::string>& input_paths,
                       const std::string& input_file_schema_path,
                       const std::string& output_results_path,
                       const std::string& output_results_schema_path);

  const std::vector<std::string>& GetInputPaths() const;
  const std::vector<std::string>& GetAssetPaths() const;

  const std::string& GetInputFileSchemaPath() const;

  const std::vector<std::vector<uint32_t>>& GetExpectedTokens() const;

  void SetLLMTokenizerPath(const std::string& path);
  std::string GetLLMTokenizerPath() const;

 private:
  void ReadOutputResultsFile();

  std::vector<std::string> asset_paths_;
  std::vector<std::string> input_paths_;
  std::string output_results_path_;
  std::string output_results_schema_path_;
  std::string input_file_schema_path_;
  std::string llm_tokenizer_path_;

 

  std::vector<std::vector<uint32_t>> expected_tokens_;
};

}  // namespace cil

#endif  // !SCENARIO_DATA_PROVIDER_H_