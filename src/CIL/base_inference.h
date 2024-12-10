/**
 * @file base_inference.h
 * @brief Abstract class for running inference using diffrent models and
 * execution providers(EPs).
 *
 * This file defines the abstract class BaseInference, which is used to run
 * inference using different models and execution providers(EPs). The class
 * includes some virtual methods that should be implemented by the derived
 * classes to support custom inference logic. The class also includes some
 * utility methods to log time, set and get device type, error message, and
 * benchmark time.
 *
 */
#ifndef BASE_INFERENCE_H_
#define BASE_INFERENCE_H_

#include <log4cxx/logger.h>

#include <nlohmann/json.hpp>
#include <vector>

#include "execution_provider.h"

namespace cil {

class API_Handler;

namespace infer {
/**
 * @class BaseInference
 * @brief Abstract class for running inference using different models and
 * execution providers(EPs).
 *
 * This class defines the abstract class BaseInference, which is used to run
 * inference using different models and execution providers(EPs). Derived
 * classes should implement the Init, Run, and Deinit methods to support custom
 * inference logic. The class includes some utility methods to log time, set and
 * get device type, error message, and benchmark time.
 *
 */

class BaseInference {
 public:
  /**
   * @brief Constructor for BaseInference class.
   *
   * Initializes the inference framework with the specified model, execution
   * provider(ep), and execution provider settings.
   * The constructor will also create a logger with the specified logger name
   * that could be used for logging the inference logs.
   *
   * @param model_path Path to the model file.
   * @param deps_dir Path to the dependencies directory containing the EP shared
   * libraries.
   * @param ep The execution provider to use for running inference.
   * @param ep_settings The settings for the execution provider.
   * @param scenario_name The unique identifier for the inference scenario name.
   * @param logger_name The name of the logger.
   * @param library_path The full path of the library supporting IHV API.
   *
   */
  BaseInference(const std::string& model_path, const std::string& deps_dir,
                EP ep, const nlohmann::json& ep_settings,
                const std::string& scenario_name,
                const std::string& logger_name,
                const std::string& library_path);
  /**
   * @brief Logs the time taken to run a specific operation.
   *
   * Logs the time taken to run a specific operation, the time will be logged in
   * milliseconds with 6 decimal points precision.
   *
   * @param desc  The description to be logged along with the duration.
   * @param st The start time of the operation.
   * @param et The end time of the operation.
   */

  void LogTime(
      const std::string& desc,
      const std::chrono::time_point<std::chrono::high_resolution_clock>& st,
      const std::chrono::time_point<std::chrono::high_resolution_clock>& et);

  virtual ~BaseInference();

  /**
   * @brief Initializes the inference framework.
   *
   * This method should be implemented by the derived classes to support custom
   * initialization logic.It should set up the necessary resources and prepare
   * the model for the inference.
   */
  // virtual void Init() = 0;

  /**
   * @brief Runs the inference using the provided input data.
   *
   * This method should be implemented by the derived classes to support custom
   * inference logic.
   * It should run the inference using the provided input data also incase of
   * any error it should set the error message using `SetErrorMessage` method.
   * The benchmark time should be set using `SetBenchmarkTime` method which will
   * be the time taken to run the inference in seconds.
   *
   * @param input_data The input data to run the inference on.
   */
  // virtual void Run(const std::vector<float>&) = 0;

  /**
   * @brief Deinitializes the inference framework.
   *
   * This method should be implemented by the derived classes to support custom
   * deinitialization logic.
   * It should release the resources and clean up the model after the inference.
   */
  // virtual void Deinit() = 0;

  /**
   * @brief Gets the benchmark time.
   *
   * This method returns the benchmark time in seconds.
   *
   * @return The benchmark time in seconds.
   */
  double GetBenchmarkTime() const;

  /**
   * @brief Gets the device type.
   *
   * This method returns the device type used for running the inference.
   *
   * @return The device type.
   */
  std::string GetDeviceType() const;

  /**
   * @brief Gets the error message.
   *
   * This method returns the error message if any error occurred during the
   * inference.
   *
   * @return The error message.
   */
  std::string GetErrorMessage() const;

  /**
   * @brief Sets the benchmark time in seconds.
   *
   * This method sets the benchmark time in seconds.
   *
   * @param benchmark_time The benchmark time in seconds.
   */
  void SetBenchmarkTime(double benchmark_time);

  /**
   * @brief Sets the device type.
   *
   * This method sets the device type used for running the inference.
   *
   * @param device_type The device type.
   */
  void SetDeviceType(const std::string& device_type);

  /**
   * @brief Sets the error message.
   *
   * This method sets the error message if any error occurred during the
   * inference.
   *
   * @param error_message The error message.
   */
  void SetErrorMessage(const std::string& error_message);

  /**
   * @brief Gets the logger.
   *
   * This method returns the logger used for logging the inference logs.
   *
   * @return The logger.
   */
  log4cxx::LoggerPtr GetLogger() const;

  /**
   * @brief Sets the metadata.
   * The metadata is a key-value pair that can be used to store additional
   * information about the inference.
   *
   * @param key The key of the metadata.
   * @param value The value to be stored under the key in the metadata.
   */
  void SetMetadata(const std::string& key, const std::any& value);

  /**
   * @brief Gets the metadata.
   *
   * This method returns the value stored under the specified key in the
   * metadata.
   *
   * @param key The key of the metadata.
   * @return The value stored under the key in the metadata.
   */
  std::any GetMetadata(const std::string& key) const;

 protected:
  const std::string scenario_name_;
  const std::string logger_name_;
  const std::string model_path_;
  const std::string deps_dir_;
  const EP ep_;

  const std::string api_type_;
  const nlohmann::json ep_settings_;

  const std::string library_path_;
  std::unique_ptr<API_Handler> api_handler_;

  // Results
  std::string device_type_;
  std::string error_message_;
  double benchmark_time_;

  // Metadata
  std::map<std::string, std::any> metadata_;

  // Logger
  log4cxx::LoggerPtr logger_;
};
}  // namespace infer
}  // namespace cil

#endif  // !BASE_INFERENCE_H_
