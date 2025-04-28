/**
 * @file main.cpp
 *
 * @brief Main entry point for the application.
 * This file contains the main entry point for the application. It is
 * responsible for parsing the command line arguments, configuring the logger,
 * and running the application.
 *
 * @details
 * The application is a command-line interface (CLI) tool that allows users to
 * run inference benchmarks on various models using different execution
 * providers. The application supports many command-line options that allow
 * users to configure the application and control its behavior, run the
 * application with the -h or --help option to display the help message and get
 * more information about the available options.
 *
 * The application performs the following steps:
 * - Parses the command line arguments.
 * - Configures the logger.
 * - Configures the scenarios.
 * - Downloads the necessary files.
 * - Prepares for inferences execution.
 * - Verifies the files.
 * - Runs the benchmarks.
 * - Displays the results.
 *
 */
#include <log4cxx/logger.h>
#include <log4cxx/logmanager.h>
#include <log4cxx/xml/domconfigurator.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "command_option.h"
#include "command_parser.h"
#include "execution_config.h"
#include "settings.h"
#include "system_controller.h"
#include "unpacker.h"
#include "utils.h"
#include "version.h"  // NOLINT

using namespace log4cxx;
using namespace log4cxx::xml;
namespace fs = std::filesystem;
namespace utils = cil::utils;

LoggerPtr loggerMain(Logger::getLogger("main"));

std::string app_description =
    "MLPerf is a benchmark application for Windows and MacOS.The "
    "goal of the benchmark is to rank various client form factors "
    "on a set of scenarios that require ML inference to function.";

void generate_command_options(CommandParser& command_parser) {
  // Add help option
  CommandOption help_option("help", 'h', "Show this help message and exit.",
                            false);
  command_parser.AddFlag(help_option);
  // Add version option
  CommandOption version_option("version", 'v',
                               "Show the application version and exit.", false);
  command_parser.AddFlag(version_option);

  // Add config option
  CommandOption config_option("config", 'c',
                              "Specify configuration file. Please refer to "
                              "README file for configuration file format.",
                              false);
  config_option.SetOptionTypeHint("file");
  config_option.SetCustomErrorMessage(
      "You must provide a path to a configuration file.");
  command_parser.AddOption(config_option);

  // Add use logger config option
  CommandOption logger_option("logger", 'l',
                              "Specify log4cxx configuration file to "
                              "override the default configuration.",
                              false);
  logger_option.SetCustomErrorMessage(
      "You must provide a log4cxx configuration file.");
  logger_option.SetOptionTypeHint("file");
  command_parser.AddOption(logger_option);

  // Add Pause option
  CommandOption pause_option(
      "pause", 'p', "Pause the application at the end of execution.", false);
  pause_option.SetCustomErrorMessage("You must provide either true or false.");
  pause_option.SetDefaultValue("true");
  command_parser.AddBooleanOption(pause_option);

  // Add list models option
  CommandOption list_models_option(
      "list-models", 'm', "List the supported models and exit.", false);
  command_parser.AddFlag(list_models_option);

  // Add download behaviour option
  CommandOption download_behaviour_option(
      "download_behaviour", 'b',
      "Controls file download behavior. 'forced' force download of all files "
      "needed even if they exist;'prompt' checks files and asks to download if "
      "missing;'skip_all' skips download and errors if files are "
      "missing;'deps_only' download the dependencies only and exit;'normal' "
      "download only the missing files or updated files.",

      false);

  download_behaviour_option.SetDefaultValue("normal");
  download_behaviour_option.SetCustomErrorMessage(
      "You must provide either forced, prompt , skip_all, deps_only, or "
      "normal.");
  command_parser.AddEnumOption(
      download_behaviour_option,
      {"forced", "prompt", "skip_all", "deps_only", "normal"});

  // Add no-cach-local-files option
  CommandOption cache_local_files_option(
      "cache-local-files", 'n',
      "true by default. If it is set to false local files specified in the "
      "config will not be copied and cached",
      false);

  cache_local_files_option.SetDefaultValue("true");
  command_parser.AddBooleanOption(cache_local_files_option);

  // Add output_dir option
  CommandOption output_dir_option(
      "output-dir", 'o',
      "Specify output directory. If not provided, default output directory is "
      "used.",
      false);
  output_dir_option.SetOptionTypeHint("folder");
  command_parser.AddOption(output_dir_option);

  // Add data-dir option
  CommandOption data_dir_option(
      "data-dir", 'd',
      "Specify directory, where all required data files will be downloaded",
      false);
  data_dir_option.SetOptionTypeHint("folder");
  command_parser.AddOption(data_dir_option);

  // Add Enumerate devices option
  CommandOption enumerate_option(
      "enumerate-devices", 'e',
      "Enumerate devices based on provided config and exit", false);
  command_parser.AddFlag(enumerate_option);

  // Add Device Id override option
  CommandOption device_id_override_option(
      "device-id", 'i', "Override device id from configuration", false);
  device_id_override_option.SetCustomErrorMessage(
      "You must provide a valid Device ID.");
  device_id_override_option.SetDefaultValue("0");
  device_id_override_option.SetValidValues(
      {"all", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"});
  command_parser.AddOption(device_id_override_option);

  // set the display order
  command_parser.SetDisplayOptionOrder(
      {"help", "version", "config", "logger", "output-dir", "data-dir",
       "enumerate-devices",
       "device-id",
       "pause",
       "list-models", "download_behaviour", "cache-local-files"});
}

int main(int argc, char* argv[]) {
#if defined(_WIN32) || defined(_WIN64)
  CommandParser command_parser("mlperf-windows.exe", app_description);
#elif defined(__APPLE__)
  CommandParser command_parser("mlperf-mac", app_description);
#endif
  generate_command_options(command_parser);
  bool is_parsed_successfully = command_parser.Process(argc, argv);
  // Check if the arguments contains any incorrect options
  if (!is_parsed_successfully) {
    std::cerr << "Incorrect options provided, aborting..." << std::endl;
    std::cerr << "Use -h or --help for more information." << std::endl;
    return 1;
  }
  // Check if non option arguments are provided
  if (command_parser.GetUnknownOptionNames().size() > 0) {
    std::cerr << "Incorrect options provided, aborting..." << std::endl;
    std::cerr << "Use -h or --help for more information." << std::endl;
    return 1;
  }
  // Check for the version option
  if (command_parser.OptionPassed("version")) {
    command_parser.ShowVersion();
    return 0;
  }
  // Check for the help option
  if (command_parser.OptionPassed("help")) {
    command_parser.ShowHelp();
    return 0;
  }
  // Check for the list models option
  if (command_parser.OptionPassed("list-models")) {
    std::clog << "List of supported models:" << std::endl;
    for (const auto& model : cli::SystemController::kSupportedModels) {
      std::clog << "- " << model << std::endl;
    }
    return 0;
  }

  // Make sure we are using the correct working directory from executable
  utils::SetCurrentDirectory(utils::GetCurrentDirectory());

  fs::path app_data_dir = utils::GetAppDefaultDataPath();
  fs::path logs_dir = app_data_dir / "Logs";
  if (!fs::exists(logs_dir)) {
    try {
      fs::create_directories(logs_dir);
    } catch (const fs::filesystem_error& e) {
      LOG4CXX_ERROR(loggerMain,
                    "Failed to create the Logs directory: " << e.what());
    }
  }

  std::shared_ptr<cil::Unpacker> unpacker = std::make_shared<cil::Unpacker>();
  std::string deps_dir = unpacker->GetDepsDir();
  // Configure the logger before displaying the version
  // Check for Log4Cxx XML config override
  std::string log_config_path = "";
  if (auto option = command_parser["logger"]; option.has_value()) {
    log_config_path = option.value();
    if (!fs::exists(log_config_path)) {
      std::cerr << "Log4cxx configuration file: " << log_config_path
                << " does not exist, the default configuration will be used."
                << std::endl;
    }
  }
  auto configureLog = [](const std::string& path) {
    if (!fs::exists(path)) return false;
    auto log_configure__status = DOMConfigurator::configure(path);
    return log_configure__status == spi::ConfigurationStatus::Configured;
  };

  bool logger_overriding_failed =
      log_config_path.empty() || !configureLog(log_config_path);

  if (logger_overriding_failed) {
    // If the logger configuration file is not found or invalid, use the default
    if (!unpacker->UnpackAsset(cil::Unpacker::Asset::kLog4cxxConfig,
                               log_config_path) ||
        !configureLog(log_config_path)) {
      std::cerr << "Failed to configure the logger with default "
                   "configurations, aborting..."
                << std::endl;
      return 1;
    } else {
      std::cout << "The default logger configuration file is used."
                << std::endl;
    }
  } else {
    // If the logger configuration file given by the user is found and valid,
    // use it
    std::cout << "The logger configuration file: " << log_config_path
              << " is used." << std::endl;
  }

  LOG4CXX_INFO(loggerMain, "Starting...");
  LOG4CXX_INFO(loggerMain, "Application Version: " << APP_VERSION_STRING << " "
                                                   << APP_BUILD_NAME);

  size_t disc_space = utils::GetAvailableDiskSpace(deps_dir);
  // display the version of the application
  if (disc_space == -1) {
    LOG4CXX_ERROR(loggerMain, "Can not determine available disc space.");
    return -1;
  } else {
    // check for the low space threshold
    size_t low_space_threshold = 100LL * 1024 * 1024;
    if (disc_space < low_space_threshold) {
      LOG4CXX_WARN(
          loggerMain,
          "Warning! Available disk space seems very low on path: "
              << deps_dir << ". At least " << low_space_threshold / 1e6
              << "mb of available space is required, currently "
              << disc_space / 1e6
              << "mb is available, the application may encounter problems.");
    }
  }

  std::string output_dir;
  if (auto option = command_parser.GetOption("output-dir");
      option.has_value()) {
    output_dir = command_parser.GetOptionValue("output-dir");
    if (!fs::exists(output_dir) || !fs::is_directory(output_dir)) {
      LOG4CXX_ERROR(loggerMain,
                    "OutputDir is not a valid directory: " << output_dir);
      return 1;
    }
  }

  std::string data_dir;
  if (command_parser.OptionPassed("data-dir")) {
    data_dir = command_parser.GetOptionValue("data-dir");
    if (!fs::exists(data_dir) || !fs::is_directory(data_dir)) {
      LOG4CXX_ERROR(loggerMain,
                    "data-dir is not a valid directory: " << data_dir);
      return 1;
    }
  }

  // Obtain the configuration file path from command line arguments. It should
  // not be empty and have a valid file path (JSON file type). We have three
  // cases:
  // 1. The user specified the config file path CONFIG option.
  // 2. The user passes the USE_INTERNAL_CONFIG option, in which case we should
  // utilize Config.json.
  // 3. If the user does not offer any options, we should use the configuration
  // file within the executable if enabled in cmake.
  std::string config_path;
  if (auto option = command_parser["config"]; option.has_value()) {
    config_path = option.value();
    if (config_path.empty()) {
      LOG4CXX_ERROR(loggerMain,
                    "Config file path is missing.\nUse -h or --help for more "
                    "information.");
      return 1;
    }

    LOG4CXX_INFO(loggerMain, "Using scenarios from " << config_path);
  } else {
    config_path = "";
    LOG4CXX_ERROR(loggerMain,
                  "Config file path is missing.\nUse -h or --help for more "
                  "information.");
    return 1;
  }

  bool enumerate_only = command_parser.OptionPassed("enumerate-devices");

  std::optional<int> device_id;
  if (auto option = command_parser["device-id"]; option.has_value()) {
    try {
      if (option.value() == "all") {
        device_id = -1;
        LOG4CXX_INFO(loggerMain, "Device id override used: all devices");
      } else {
        device_id = std::stoi(option.value());
        LOG4CXX_INFO(loggerMain,
                     "Device id override used: " << device_id.value());
      }
    } catch (const std::invalid_argument&) {
      LOG4CXX_ERROR(loggerMain, "Invalid device id: " << option.value());
      return 1;
    }
  }

  cli::SystemController controller(config_path, unpacker, output_dir, data_dir);
  LOG4CXX_INFO(loggerMain, "Configuring scenarios...\n");
  if (!controller.Config()) {
    LOG4CXX_ERROR(loggerMain, "Failed to configure scenarios, aborting...");
    return 1;
  }

  if (command_parser.OptionPassed("download_behaviour")) {
    auto download_behaviour_option_value =
        command_parser.GetOptionValue("download_behaviour");
    controller.SetSystemDownloadBehavior(download_behaviour_option_value);
  }

  if (command_parser.OptionPassed("cache-local-files")) {
    auto cache_local_files =
        command_parser.GetOptionValue("cache-local-files") ==
        "true";  // default value is true
    controller.SetSystemCacheLocalFiles(cache_local_files);
  }

  auto logger = [](cli::SystemController::LogLevel log_level,
                   const std::string& message) {
    using enum cli::SystemController::LogLevel;
    switch (log_level) {
      case kInfo:
        LOG4CXX_INFO(loggerMain, message);
        break;
      case kWarning:
        LOG4CXX_WARN(loggerMain, message);
        break;
      case kError:
        LOG4CXX_ERROR(loggerMain, message);
        break;
    }
  };

  controller.Run(logger, enumerate_only, device_id);

  log4cxx::LogManager::shutdown();
  std::string pause_option_value =
      command_parser.GetOptionValue("pause");  // the Default value is true
  if (pause_option_value == "true") {
    std::cout << "\nPress any key to continue...";
    getchar();  // Wait for a key press
  }

  return 0;
}
