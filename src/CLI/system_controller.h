#ifndef SYSTEM_CONTROLLER_H_
#define SYSTEM_CONTROLLER_H_

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cil {
class ExecutionConfig;
class Unpacker;
class EPDependenciesManager;
class ScenarioDataProvider;
class FileSignatureVerifier;
class ExecutionProviderConfig;
class ScenarioConfig;
class BenchmarkLogger;
}  // namespace cil

namespace cli {
/**
 * @class SystemController
 *
 * @brief This class is responsible for controlling and managing different
 * stages of the system, such as configuration, preparation, download, data
 * verification, benchmarking, and displaying results.
 */
class SystemController {
 public:
  /**
   * @brief Constructor for SystemController class.
   * @param json_config_path Path to the JSON configuration file.
   * @param unpacker Unpacker object to unpack the necessary files.
   * @param output_dir The output dir to store results.json (default path is
   * used when empty).
   * @param data_dir Directory where all downloaded files will be stored
   * (default path is used when empty).
   */
  SystemController(const std::string& json_config_path,
                   std::shared_ptr<cil::Unpacker> unpacker,
                   const std::string& output_dir = {},
                   const std::string& data_dir = {});
  ~SystemController();
  /**
   * @brief Loads and verifies the tool configuration.
   *
   * This method is responsible for loading the application's configuration from
   * a specified source and verifying its correctness.
   *
   * @return True if the configuration is loaded and verified successfully,
   * false otherwise.
   */
  bool ConfigStage();
  /**
   * @brief Downloads the necessary files for specified scenarios indexed.
   *
   * This method downloads the necessary files for the specified scenarios,
   * including the model, input, and asset files. Additionally, it downloads the
   * necessary dependencies for the execution providers (eps). This stage is
   * also responsible for unpacking the downloaded files and preparing them for
   * the next stages.
   *
   * @param model_index Index of the model/scenario to download its files.
   * @param download_behavior Controls the behavior of the download stage.
   * eps.
   * @return True if the download stage is successful, false otherwise.
   */
  bool DownloadStage(int model_index, const std::string& download_behavior);
  /**
   * @brief Prepares the configuration for the supplied scenarios.
   *
   * This method performs several tasks to prepare the application:
   * - Checks if the specified model is supported.
   * - Prepares the dependencies required by execution providers (eps).
   * - Loads the verification data for each scenario.
   *
   * @return True if the preparation stage is successful, false otherwise.
   */
  bool PreparationStage(int model_index);
  /**
   * @brief Verifies the data for the specified scenarios.
   *
   * This method verifies the data for the specified scenarios, including all
   * the downloaded files. It checks the file signatures and hashes against the
   * provided data verification file for each scenario to ensure the integrity
   * of the downloaded files.
   *
   * @param model_index Index of the model/scenario to verify its data.
   * @return True if the data verification stage is successful, false otherwise.
   */
  bool DataVerificationStage(int model_index);
  /**
   * @brief Benchmarks the specified scenarios.
   *
   * This method benchmarks the specified scenarios using the provided execution
   * providers (eps) and displays the results, also logs the result to
   * logs/results.json. the benchmarking process includes the following steps:
   * - Loading the model and input files.
   * - Running the benchmarking process.
   * - collecting system information.
   * - Logging the results.
   *
   * @param model_index Index of the model/scenario to benchmark.
   * @return True if the benchmark stage is successful, false otherwise.
   */
  bool BenchmarkStage(int model_index);
  /**
   * @brief Displays the results of all the benchmarked scenarios.
   *
   * This method is responsible for displaying the benchmarking for all the
   * scenarios.
   */
  void DisplayResultsStage();
  /**
   * @brief Retrieves the list of configuration scenarios.
   *
   * This method returns a constant reference to a vector containing the
   * configuration scenarios used in the application. These scenarios are
   * defined in the cil::ScenarioConfig structure.
   *
   *  @return A constant reference to a vector of cil::ScenarioConfig containing
   * the configuration scenarios.
   */
  const std::vector<cil::ScenarioConfig>& GetScenarios() const;
  /**
   * @brief Retrieves the temporary path used by the application.
   * This method returns the path to a temporary directory used by
   * the application for various purposes. The temporary path is used for tasks
   * such as unpacking required files, storing temporary data, and managing
   * DLLs.
   *
   * @return A string containing the temporary path used by the application.
   */
  const std::string& GetSystemConfigTempPath() const;

  /*
   * @brief Retrieves the current download behavior configuration.
   *
   * This method fetches the system-defined configuration for download behavior.
   *
   * @return A constant reference to a string containing the download behavior configuration.
   */
  const std::string& GetDownloadBehavior() const;

  /**
   * @brief List of supported model names.
   *
   * This constant contains the list of supported model names, any model name
   * not in this list will be considered as unsupported.
   */
  static const std::vector<std::string> kSupportedModels;

 private:
  std::filesystem::path GetEPParentLocation(
      const cil::ExecutionProviderConfig& ep_config) const;

  bool PrepareDirectMLAdaptersIfNeeded(
      std::unordered_set<std::string>& not_prepared_ep_names,
      const std::string_view& target_ep_name,
      const std::string_view& library_path,
      bool update_config = false);

  static void InterruptHandler(int sig);

  static std::atomic<bool> interrupt_;

  const std::string json_config_path_;
  std::unique_ptr<cil::ExecutionConfig> config_;
  std::shared_ptr<cil::Unpacker> unpacker_;
  std::unique_ptr<cil::EPDependenciesManager> ep_dependencies_manager_;

  std::string output_results_schema_path_;
  std::string data_verification_file_schema_path_;
  std::string input_file_schema_path_;
  std::string data_dir_;

  std::unordered_map<int, std::vector<std::string>> model_file_paths_;
  std::map<int, std::vector<std::string>> input_file_paths_;
  std::map<int, std::vector<std::string>> asset_file_paths_;
  std::map<int, std::string> output_results_file_paths_;
  std::map<int, std::shared_ptr<cil::ScenarioDataProvider>>
      scenario_data_providers_;
  std::map<int, std::shared_ptr<cil::FileSignatureVerifier>>
      file_signature_verifiers_;

  std::map<std::string, std::string> path_to_source_map_;
  std::map<std::string, std::string> source_to_path_map_;

  std::vector<std::pair<cil::ExecutionProviderConfig, std::string>>
      prepared_eps_;

  std::unique_ptr<cil::BenchmarkLogger> results_logger_;
  bool config_verified_{false};

};

}  // namespace cli

#endif  // !SYSTEM_CONTROLLER_H_
