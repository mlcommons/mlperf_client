#include "prompt_file_reader.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace cil::infer {

PromptFileReader::LoadResult PromptFileReader::Load(
    const std::string& file_path) {
  LoadResult result;

  if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
    result.error =
        "Input path does not exist or is not a file, path: " + file_path;
    return result;
  }

  std::ifstream input_file(file_path);
  try {
    input_file >> result.json;
  } catch (const nlohmann::json::parse_error& e) {
    result.error = "Failed to parse input json " + file_path +
                   ", error: " + std::string(e.what());
    return result;
  }
  input_file.close();

  result.ok = true;
  return result;
}

int PromptFileReader::CountPromptsInFile(const std::string& file_path) {
  std::ifstream input_file(file_path);
  nlohmann::json input_json;
  input_file >> input_json;
  input_file.close();
  return static_cast<int>(input_json["prompts"].size());
}

int PromptFileReader::LoadAndValidateAll(
    const std::vector<std::string>& input_paths, const Validator& validator,
    std::string& out_error) {
  int total_prompts = 0;

  for (const std::string& path : input_paths) {
    auto loaded = Load(path);
    if (!loaded.ok) {
      out_error = loaded.error;
      return -1;
    }

    if (validator && !validator(loaded.json, path)) return -1;

    if (loaded.json.contains("prompts") && loaded.json["prompts"].is_array())
      total_prompts += static_cast<int>(loaded.json["prompts"].size());
  }

  return total_prompts;
}

}  // namespace cil::infer
