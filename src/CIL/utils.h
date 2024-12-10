/**
 * @file utils.h
 * @brief Utility functions for various common tasks.
 *
 * This file contains a collection of utility functions that are used
 * across different parts of the application. These functions provide
 * commonly needed functionality, such as
 * - Adding and removing paths to the library search path
 * - checking the available disk space
 * - creating directories and normalizing paths and many more.
 */
#pragma once

#include <log4cxx/logger.h>

#include <filesystem>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#undef GetCurrentDirectory
#undef SetCurrentDirectory
#undef CreateDirectory
#undef GetTempPath

namespace fs = std::filesystem;

namespace cil {
namespace utils {
/**
 * @brief Retrieves the path to the temporary directory.
 *
 * This function returns the path to a temporary directory to be used by the
 * tool if the user does not specify a temporary directory. The temporary
 * directory is used for tasks such as unpacking required files, storing
 * temporary data, and managing DLLs.
 *
 * @return A path to the temporary directory.
 */

inline fs::path GetAppDefaultTempPath() {
#if defined(DEBUG_BUILD)
  return (fs::current_path() / "deps");
#else
  return (std::filesystem::temp_directory_path() / "MLPerf");
#endif
}

/**
 * A handle returned by AddLibraryPath function and used to remove the
 * directory from the search path.
 */
struct LibraryPathHandle {
#ifdef _WIN32
  DLL_DIRECTORY_COOKIE cookie_;
  bool IsValid() const { return cookie_ != nullptr; }
#else
  fs::path path_;
  bool IsValid() const { return !path_.empty(); }
#endif
};

/**
 * @brief Adds a directory to the search path for DLLs on Windows or shared
 * libraries on Unix based systems.
 *
 * This function adds a directory to the search path used by the system to
 * locate libraries. It returns a `LibraryPathHandle`, which can be used to
 * remove the directory from the search path.
 *
 * @param path The directory path to be added.
 * @return A valid `LibraryPathHandle` if the directory is successfully added.
 */
LibraryPathHandle AddLibraryPath(const fs::path& path);
/**
 * @brief Removes a directory from the search path for DLLs on Windows or
 * shared libraries on Unix.
 *
 * This function removes a directory from the search path used by the system to
 * locate libraries.
 *
 * @param handle The `LibraryPathHandle` that identifies the directory to be
 * removed.
 * @return True if the directory is successfully removed, false otherwise.
 */
bool RemoveLibraryPath(const LibraryPathHandle& handle);
/**
 * @brief Retrieves the current working directory.
 *
 * This function returns the current working directory of the application.
 *
 * @return A string containing the current working directory.

*/
std::string GetCurrentDirectory();
/**
 * @brief Sets the current working directory to the directory of the executable.
 *
 * This function retrieves the directory path of the currently running
 * executable and sets the current working directory to this path. This ensures
 * that the application operates relative to its own location, regardless of the
 * directory from which it was launched. For Windows, it uses
 * `SetCurrentDirectoryA`, and for other systems, it uses `chdir`.
 */
void SetCurrentDirectory();
/**
 * @brief Creates a directory at the specified path.
 *
 * This function creates a directory at the specified path. If the directory
 * already exists, it does nothing also it handles any exceptions that may occur
 * during the creation process.
 *
 * @param directoryPath The path to the directory to be created.
 * @return True if the directory is successfully created, false otherwise.
 */
bool CreateDirectory(const std::string& directoryPath);
/**
 * @brief Creates a unique directory under the specified base directory.
 *
 * The directory name is created by concatenating the current date and time, the
 * PID of the process, and a number representing the number of attempts, to
 * ensure the directory name is unique.
 *
 * @param base_dir The path to the base directory where the unique directory
 * will be created.
 * @param out_path The path to the created unique directory.
 * @param max_attempts The maximum number of attempts to create a unique
 * directory.
 * @return True if the directory is successfully created, false otherwise.
 */
bool CreateUniqueDirectory(const fs::path& base_dir, fs::path& out_path,
                           int max_attempts = 100);
/**
 * @brief Saves a boolean flag to the system's registry or configuration store.
 *
 * This a cross-platform function that saves a boolean flag to the system's
 * configuration storage. On Windows, it uses the registry, and on MacOs it uses
 * the user preferences.
 *
 * @param settings_path The path to the settings in the system's configuration
 * store.
 * @param value_name The name of the value to be saved.
 * @param flag The boolean flag to be saved.
 */
void SaveFlagToSettings(const std::string& settings_path,
                        const std::string& value_name, bool flag);
/**
 * @brief Reads a boolean flag from the system's registry or configuration
 * store.
 *
 * This a cross-platform function that reads a boolean flag from the
 * system's configuration storage. On Windows, it uses the registry, and on
 * MacOs it uses the user preferences.
 *
 * @param settings_path The path to the settings in the system's configuration
 * store.
 * @param value_name The name of the value to be read.
 * @return The boolean flag read from the system's configuration storage.
 */
bool ReadFlagFromSettings(const std::string& settings_path,
                          const std::string& value_name);
/**
 * @brief Retrieves the current date and time as a string.
 *
 * This function returns the current date and time as a string in the specified
 * format. The default format is `"%Y-%m-%d %X"`.
 *
 * @param format The format string to be used for the date and time.
 * @return A string containing the current date and time.
 */
std::string GetCurrentDateTimeString(const std::string& format = "%Y-%m-%d %X");
/**
 * @brief Converts a time point to a formatted local time string.
 *
 * This function converts a given high-resolution clock time point to a string
 * representing the local time in the format %Y-%m-%d %H:%M:%S", followed by the
 * timezone information.
 *
 * @param time_point The high-resolution clock time point to be converted.
 * @return A string representing the local time in the format %Y-%m-%d
 * %H:%M:%S", followed by the timezone information.
 */
std::string GetDateTimeString(
    std::chrono::high_resolution_clock::time_point time_point);
/**
 * @brief Retrieves the width of the console window.
 *
 * This function returns the width of the console window in characters.
 *
 * @return The width of the console window in characters.
 */
int GetConsoleWidth();

#if defined(__APPLE__)
bool IsDebuggerPresent();
#endif
/**
 * @brief Retrieves the available disk space at the specified path.
 *
 * This function returns the available disk space in bytes at the specified
 * path.
 *
 * @param path The path to the directory for which the available disk space is
 * to be retrieved.
 * @return The available disk space in bytes at the specified path.
 */
size_t GetAvailableDiskSpace(const std::string& path);
/**
 * @brief Retrieves File Name from the given path.
 *
 * This function returns the file name from the given path.
 *
 * @param path The path to the file.
 * @return The file name from the given path.
 */
std::string GetFileNameFromPath(const std::string& path);
/**
 * @brief Checks if a file exists at the specified path.
 *
 * This function checks if a file exists at the specified path.
 *
 * @param path The path to the file.
 * @return True if the file exists, false otherwise.
 */
bool FileExists(const std::string& path);
/**
 * @brief Computes the SHA-256 hash of the contents of a specified file.
 *
 * it computes the SHA-256 hash of the contents of a specified file and Logs the
 * errors using the provided log4cxx logger; may be nullptr to disable
 *
 * @param file_path The path to the file.
 * @param logger  Log4cxx logger to log errors; may be nullptr to disable
 * logging.
 * @return SHA256 hash of the file in hexadecimal string format or an empty
 * string if an error occurred.
 */
std::string ComputeFileSHA256(const std::string& file_path,
                              log4cxx::LoggerPtr logger);

/**
 * @brief Removes long static paths from the error message.
 *
 * This function removes long static paths from the error message and replaces
 * them by the file name only, to make the error message more readable.
 *
 * @param error_message The error message to be cleaned.
 * @return The cleaned error message.
 * */
std::string CleanErrorMessageFromStaticPaths(const std::string& error_message);
/**
 * @brief Checks if the given path is valid.
 *
 * This function checks if the given path is valid by applying regex and check
 * if the path is containing invalid characters based on the OS.
 *
 * @param path The path to be checked.
 * @return True if the path is valid, false otherwise.
 */
bool IsValidPath(const std::string path);
/**
 * @brief Normalizes the given filesystem path by replacing specific escape
 * sequences and backslashes.
 *
 * This function replaces specific escape sequences (e.g., '\a' with '\\a') in
 * the path and then replaces all backslashes with forward slashes to create a
 * normalized path.
 *
 * @param path The path to be normalized.
 * @return A string representing the normalized path.
 */
std::string NormalizePath(const std::string& path);
/**
 * @brief Converts the input string to its lowercase equivalent.
 *
 * This function takes a string as input and returns a new string
 * where all uppercase characters have been converted to lowercase.
 *
 * @param input_string The string to be converted to lowercase.
 * @return A new string representing the lowercase version of the input string.
 */
std::string StringToLowerCase(const std::string& input_string);
/**
 * @brief Checks if the given string is a valid UTF-8 string.
 *
 * This function checks if the given string is a valid UTF-8 string.
 *
 * @param str The string to be checked.
 * @return True if the string is a valid UTF-8 string, false otherwise.
 */
bool IsUtf8(const std::string& str);

/**
 * @brief Sanitizes the given string to a valid UTF-8 string.
 *
 * This function sanitizes the given string to a valid UTF-8 string by removing
 * invalid characters and replacing them with the Unicode replacement character.
 *
 * @param str The string to be sanitized.
 * @return A string representing the sanitized UTF-8 string.
 */
std::string SanitizeToUtf8(const std::string& str);

/**
 * @brief List of available vendor IDs.
 *
 * This enum class represents the available vendor IDs.
 */
enum class VendorID {
  kNVIDIA,
  kIntel,
  kAMD,
  kQualcomm,
  kApple,
  kMellanox,
  kUnknown
};

/**
 * @brief Get available vendors IDs.
 *
 * This function returns a vector of available vendor IDs.
 *
 * @return A vector of available vendor IDs.
 */
std::vector<VendorID> GetAvailableVendorIDs();

/*
 *
 * @brief Checks if the given vendor ID is available.
 *
 * This function checks if the given vendor ID is available.
 *
 * @param vendor_id The vendor ID to be checked.
 * @return True if the vendor ID is available, false otherwise.
 */
bool SupportsVendorID(VendorID vendor_id);

/**
 * @brief Remaps the given path to the base directory.

  * This function remaps the given path to the base directory by appending the
  * base directory to the path if the path is not an absolute path.
  *
  * @param path The path to be remapped.
  * @param base_dir The base directory to be appended to the path.
  * @return A string representing the remapped path.
 */
std::string PrependBaseDirIfNeeded(const std::string& path,
                                   const std::string& base_dir);

/**
  * @brief Formats the given duration in milliseconds to a string.

  * This function formats the given duration in milliseconds to a string in the
  * format "HH:MM:SS.mmm".
  *
  * @param duration The duration to be formatted.
  * @return A string representing the formatted duration.
  */
std::string FormatDuration(const std::chrono::steady_clock::duration& duration,
                           bool with_millisec = true);

/**
 * @brief Checks if the given execution provider is supported on this platform.
 *
 * This function checks if the given execution provider is supported on this
 * platform by checking the model name and the execution provider name.
 * For some EPs it will check if there is hardware compatibility too.
 *
 * @param model_name The name of the model.
 * @param ep_name The name of the execution provider.
 * @return True if the execution provider is supported on this platform, false
 * otherwise.
 */
bool IsEpSupportedOnThisPlatform(const std::string_view& model_name,
                                 const std::string_view& ep_name);

/**
* @brief Cleans and trims the given string.
*
* This function cleans and trims the given string by removing any null characters and
* leading and trailing whitespaces.
* 
* @param str The string to be cleaned and trimmed.
* @return A string representing the cleaned and trimmed string.
*/
std::string CleanAndTrimString(std::string str);

}  // namespace utils
}  // namespace cil
