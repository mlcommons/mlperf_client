#pragma once

#include <log4cxx/logger.h>

#include <filesystem>
#include <string>

namespace cil {

/**
 * @file portable_python.h
 * @brief Locating and enabling the `python` interpreter used by the `execute`
 * tool.
 *
 * Supported desktop builds bundle a portable interpreter and put it on PATH;
 * sandboxed/iOS builds can't launch external processes.
 */

/**
 * @brief Full URL of the portable Python zip for this build target.
 * @return The zip URL, or an empty string on unsupported targets.
 */
std::string GetPortablePythonAssetUrl();

/**
 * @brief Name of the directory the archive extracts into — the zip's stem
 * (e.g. "python-3.13.13-win-x64").
 * @return The extraction directory name.
 */
std::string GetPortablePythonDirName();

/**
 * @brief Put `python` on PATH for scenarios that run it, re-applying when
 * @p configured_dir changes (the GUI can update it between runs).
 *
 * Uses @p configured_dir (SystemConfig's PythonPath) if non-empty, else the
 * bundled interpreter found by searching @p search_dir and its parent.
 *
 * @param search_dir Directory whose subtree (itself and its parent) is searched
 * for the bundled interpreter.
 * @param configured_dir Configured interpreter directory (SystemConfig's
 * PythonPath), empty to use the bundled Python, or "system" to leave `python`
 * resolution to the pre-existing PATH (undoing any earlier prepend).
 * @param log Logger used to report the failure reason.
 * @return @c false (logging the reason) on failure — no interpreter at a
 * configured path, missing bundled Python, or a configured path on a build that
 * can't run it; @c true otherwise.
 */
bool EnsurePortablePythonOnPath(const std::filesystem::path& search_dir,
                                const std::string& configured_dir,
                                const log4cxx::LoggerPtr& log);

}  // namespace cil
