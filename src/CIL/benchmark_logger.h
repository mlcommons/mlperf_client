#ifndef BENCHMARK_LOGGER_H_
#define BENCHMARK_LOGGER_H_

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

namespace cil {

struct BenchmarkResult;
class SystemInfoProvider;
/**
 * @class BenchmarkLogger
 * @brief This class is responsible for logging and displaying benchmark results.
 * 
 * This class is responsible for logging and displaying benchmark results as well as system information.
 * It provides methods to append benchmark results, display all results, and clear the results.
 * 
*/
class BenchmarkLogger {
 public:
  /**
   * @brief Constructor for BenchmarkLogger class.
   * 
   * @param output_dir The output dir to store results.json (default path is used 
   * when empty).
   * 
   */
  explicit BenchmarkLogger(const std::string& output_dir);
  ~BenchmarkLogger();
  /**
   * @brief Appends the benchmark result to the results list and logs it to the file.
   * 
   * @param result The benchmark result to be appended.
  */
  bool AppendBenchmarkResult(const BenchmarkResult& result);

  /**
   * @brief Displays all the benchmark results in the console in a formatted way.
   * 
   */
  void DisplayAllResults();

  /**
   * @brief Clears all the benchmark results.
   * 
   */
  void Clear();

  /**
   * @brief Set application version string
   * 
   */
  void SetApplicationVersionString(std::string application_version_string);

  /**
   * @brief Set if verified config flag
   * 
   */
  void SetConfigVerified(bool is_verified);

  /**
   * @brief Set config experimental flag
   *
   */
  void SetConfigExperimental(bool is_experimental);

  /**
   * @brief Get the benchmark results.
   *
   */
  const std::vector<BenchmarkResult>& GetResults() const;

  /**
   * @brief Read results from file
   *
   */
  static std::vector<BenchmarkResult> ReadResultsFromFile(
      const std::string& filename);

  static void RemoveResultsFromFile(
      const std::string& filename,
      const std::unordered_set<int>& rows_to_remove);

 private:
  nlohmann::ordered_json BenchmarkResultToJson(const BenchmarkResult& result);
  void WriteResults(const nlohmann::ordered_json& json_array);

  std::vector<BenchmarkResult> GetResults(
      const std::map<std::string, BenchmarkResult>& results);

  bool config_verified_{false};
  bool config_experimental_{false};
  std::string application_version_string_;
  std::string filename_;
  std::vector<BenchmarkResult> results_;
  SystemInfoProvider* system_info_provider_;
};

}  // namespace cil

#endif  // BENCHMARK_LOGGER_H_
