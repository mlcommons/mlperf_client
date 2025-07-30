/**
 * @file unpacker.h
 *
 * @brief The Unpacker class used to unpack files that previously packed inside
 * the application during the build process.
 *
 * @details The Unpacker class is used to extract many files, including:
 * - log4cxx built-in configuration file.
 * - Configuration schema file.
 * - Built-in output results file.
 * - Output results schema file.
 * - Built-in data verification file.
 * - Data verification schema file.
 * - Execution provider dependencies configuration file.
 * - Execution provider dependencies configuration schema file.
 * - EP packed files.
 * also the Unpacker class can be used to extract files from a ZIP file.
 */

#ifndef UNPACKER_H_
#define UNPACKER_H_

#include <filesystem>
#include <string>
#include <vector>

namespace cil {
/**
 * @class Unpacker
 *
 * @brief The Unpacker class used to unpack files that previously packed inside
 * the application during the build process.
 */
class Unpacker {
 public:
  enum class Asset {
    kLog4cxxConfig,  // Default built-in file used to configure the log4cxx
    // library.
    kConfigSchema,  // This file is used to validate the configuration file.
    kOutputResultsSchema,  // This file is used to validate the output results
    // file.
    kDataVerificationFile,  // This file is used for data verification, It
    // contains the hash of all the external files used
    // in the benchmark like models, input data, and
    // assets.
    kDataVerificationFileSchema,  // This file is used to validate the data
    // config verification file.
    kConfigVerificationFile,  // This file is used to detect whether
    // the configuration file is tested or not.
    // config experimental verification file.
    kConfigExperimentalVerificationFile,  // This file is used to detect whether
                                          // the config is experimental or not
    kLLMInputFileSchema,  // This file is used to validate the Llama2 input
    // file.
    kEPDependenciesConfig,  // This file is used to define the dependencies of
    // the execution providers.
    kEPDependenciesConfigSchema,  // This file is used to validate the execution
    // provider dependencies configuration file.
    kVendorsDefault,  // This file is a .zip file which contains vendors default
                      // configurations
    kNativeOpenVINO,  // This file is used to run benchmarks with Native
                      // OpenVINO DLLs.
    kOrtGenAI,  // This file is used to run benchmarks with ORT Gen-AI DLLs.
    kOrtGenAIRyzenAI,   // This file is used to run benchmarks with ORT Gen-AI RyzenAI DLLs.
    kWindowsML,  // This file is used to run benchmarks with WindowsML DLLs.
    kGGML_EPs,       // This file is used to run benchmarks with GGML
    kGGML_Vulkan,    // This file is used to run benchmarks with GGML Vulkan
    kGGML_CUDA,      // This file is used to run benchmarks with GGML CUDA
    kGGML_Metal,     // This file is used to run benchmarks with GGML Metal
    kNativeQNN,  // This file is used to run benchmarks with Native QNN
    kMLX,            // This file is used to run benchmarks with MLX
  };

  Unpacker();
  ~Unpacker();
  /**
   * @brief Set the directory where the unpacked files will be stored.
   *
   * @param deps_dir The directory path.
   */
  void SetDepsDir(const std::string& deps_dir, bool remove_old = true);
  /**
   * @brief Get the directory where the unpacked files will be stored.
   *
   * @return The directory path.
   */
  std::string GetDepsDir() const;
  /**
   * @brief Unpack an asset.
   *
   * This method will extract the asset and return the path to the unpacked
   * file.
   *
   * @param asset The asset to unpack.
   * @param path The path to the unpacked asset.
   * @param forced If true, the asset will be unpacked even if it already
   * exists.
   * @return True if the asset was successfully unpacked, false otherwise.
   */
  bool UnpackAsset(Asset asset, std::string& path, bool forced = false);
  /**
   * @brief Get the size of all the unpacked files.
   *
   * @return The size of all the unpacked files.
   */
  size_t GetAllDataSize() const;
  /**
   * @brief Unpack files from a ZIP file with a timeout.
   *
   * This method will extract all the files from the ZIP file to the destination
   * directory with a timeout if not 0
   * The timeout will be calculated based on the size of the ZIP file.
   *
   * @param zip_file The path to the ZIP file.
   * @param dest_dir The destination directory.
   * @param return_relative_paths If true, the function returns paths relative
   * to the dest_dir
   * @param timeout_ms Timeout duration in milliseconds for the unpacking
   * process.
   *                  - (<0):  Default timeout based on file size
   *                  - (=0):  No timeout
   *                  - (>0): Custom timeout in milliseconds
   * @return A vector of strings that contains the paths of the unpacked files.
   */
  static std::vector<std::string> UnpackFilesFromZIP(
      const std::string& zip_file, const std::string& dest_dir,
      bool return_relative_paths = false, int64_t timeout_ms = -1);

 private:
  bool ExtractFileFromMemory(const unsigned char* data[], size_t data_size,
                             unsigned int parts, const unsigned part_sizes[],
                             const std::string& file_path);

  std::filesystem::path deps_dir_;
  std::vector<std::filesystem::path> unpacked_files_;
};

}  // namespace cil

#endif  // !UNPACKER_H_