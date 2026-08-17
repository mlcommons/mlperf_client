/**
 * @file prompt_file_reader.h
 *
 * @brief Utility for loading, parsing, and counting prompt input files.
 *
 * Domain executors compose this class to handle prompt file I/O.
 */
#ifndef PROMPT_FILE_READER_H_
#define PROMPT_FILE_READER_H_

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cil::infer {

class PromptFileReader {
 public:
  struct LoadResult {
    bool ok = false;
    std::string error;
    nlohmann::json json;
  };

  /** Load and parse a single JSON file. */
  static LoadResult Load(const std::string& file_path);

  /** Count prompts in a JSON input file (reads "prompts" array length). */
  static int CountPromptsInFile(const std::string& file_path);

  /**
   * Load all input files, call @p validator on each parsed JSON, and
   * return the total prompt count.  On any failure, sets @p out_error
   * and returns -1.
   */
  using Validator =
      std::function<bool(const nlohmann::json& json, const std::string& path)>;
  static int LoadAndValidateAll(const std::vector<std::string>& input_paths,
                                const Validator& validator,
                                std::string& out_error);
};

}  // namespace cil::infer

#endif  // PROMPT_FILE_READER_H_
