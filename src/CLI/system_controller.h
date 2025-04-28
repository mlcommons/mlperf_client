#ifndef SYSTEM_CONTROLLER_H_
#define SYSTEM_CONTROLLER_H_

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <string>

namespace cil {
class ExecutionConfig;
class Unpacker;
class EPDependenciesManager;
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
                   const std::string& output_dir, const std::string& data_dir);
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
  bool Config();
  /**
   * @brief Custom stage which displays the results of all the benchmarked
   * scenarios.
   *
   * This method is responsible for displaying the benchmarking for all the
   * scenarios.
   */
  void DisplayResultsStage(cil::BenchmarkLogger& results_logger) const;
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
   * @return A constant reference to a string containing the download behavior
   * configuration.
   */
  const std::string& GetSystemDownloadBehavior() const;

  /*
   * @brief Sets the download behavior configuration.
   *
   * This method sets the download behavior configuration to the provided value.
   *
   * @param download_behavior The download behavior configuration value.
   *
   */
  void SetSystemDownloadBehavior(const std::string& download_behavior);

  /*
   * @brief Retrieves the value for CacheLocalFiles parameter from the config
   *
   * If it is set to false local files specified in the config will not be
   * copied and cached
   *
   * @return A boolean value containing the value of CacheLocalFiles config
   */
  bool GetSystemCacheLocalFiles() const;

  /*
   * @brief Sets the value for CacheLocalFiles parameter from the command line
   * if provided, otherwise the value from the system config is used or true.
   *
   * @param cache_local_files The value of CacheLocalFiles parameter from the
   * command line if provided, otherwise the value from the system config is
   * used or false.
   *
   */
  void SetSystemCacheLocalFiles(bool cache_local_files);

  enum class LogLevel { kInfo, kWarning, kError };
  using Logger = std::function<void(LogLevel, std::string)>;

  /*
   * @brief Runs the benchmark stages.
   *
   * This method is responsible for running the benchmark stages, including
   * download, preparation, data verification, benchmarking, and
   * displaying results.
   *
   * @param download_behaviour_option_value The download behaviour option value.
   * @param cache_local_files The value of CacheLocalFiles parameter from the
   * command line if provided, otherwise the value from the system config is
   * used or false.
   * @param logger The logger function to log messages for main application.
   * The controller will separately log messages for each stage using log4cxx
   * library. This could be used to log messages to console for the user using
   * standard output.
   *
   * @return True if the benchmark stages are successful, false otherwise.
   */
  bool Run(const Logger& logger, bool enumerate_devices = false,
           std::optional<int> device_id = std::nullopt) const;

  /**
   * @brief List of supported model names.
   *
   * This constant contains the list of supported model names, any model name
   * not in this list will be considered as unsupported.
   */
  static const std::vector<std::string> kSupportedModels;

 private:
  static void InterruptHandler(int sig);

  static std::atomic<bool> interrupt_;

  const std::string json_config_path_;
  std::unique_ptr<cil::ExecutionConfig> config_;
  std::shared_ptr<cil::Unpacker> unpacker_;
  std::unique_ptr<cil::EPDependenciesManager> ep_dependencies_manager_;

  std::string output_results_schema_path_;
  std::string data_verification_file_schema_path_;
  std::string input_file_schema_path_;

  const std::string output_dir_;
  const std::string data_dir_;
};

}  // namespace cli

#endif  // !SYSTEM_CONTROLLER_H_
