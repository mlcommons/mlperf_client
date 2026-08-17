#include "portable_python.h"

#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <cstdlib>  // getenv, setenv
#include <fstream>
#endif

// iOS builds can't run external executables, so they don't bundle the
// portable interpreter.
#if TARGET_OS_IOS
#define CIL_PORTABLE_PYTHON_SUPPORTED 0
#else
#define CIL_PORTABLE_PYTHON_SUPPORTED 1
#endif

// Injected by CMake; fallback lets the file compile standalone (Python
// disabled).
#ifndef MLPERF_PORTABLE_PYTHON_URL
#define MLPERF_PORTABLE_PYTHON_URL ""
#endif

namespace fs = std::filesystem;

namespace cil {

namespace {

#if CIL_PORTABLE_PYTHON_SUPPORTED

// The bundled interpreter, found by searching `search_dir` and its parent for
// `<zip-stem>/python` (the download stage extracts the archive under the
// scenario data root), or nullopt if not found.
std::optional<fs::path> FindPythonDir(const fs::path& search_dir,
                                      const fs::path& configured_dir) {
  // The interpreter inside `python_dir`, or empty if it holds none.
  static auto resolveInterpreter = [](const fs::path& python_dir) {
#ifdef _WIN32
    const fs::path exe = python_dir / "python.exe";
#else
    const fs::path exe = python_dir / "bin" / "python3";
#endif
    std::error_code ec;
    return fs::exists(exe, ec) ? exe : fs::path{};
  };

  if (!configured_dir.empty()) {
    if (!resolveInterpreter(configured_dir).empty()) return configured_dir;
    if (fs::path nested = configured_dir / "python";
        !resolveInterpreter(nested).empty())
      return nested;

  } else {
    const fs::path sub = GetPortablePythonDirName();
    if (fs::path python = search_dir / sub / "python";
        !resolveInterpreter(python).empty())
      return python;
    if (fs::path python = search_dir.parent_path() / sub / "python";
        !resolveInterpreter(python).empty())
      return python;
  }
  return std::nullopt;
}

#ifndef _WIN32
// Our zip extractor writes Unix symlinks as plain files containing the
// target text (e.g. `python3` containing `python3.13`); repair that back
// into a real symlink.
bool RepairSymlinkStub(const fs::path& path) {
  std::error_code ec;
  if (fs::is_symlink(path, ec) || !fs::is_regular_file(path, ec)) return false;
  // A target filename is a handful of bytes; real interpreter binaries are
  // megabytes.
  const auto size = fs::file_size(path, ec);
  if (ec || size == 0 || size > 256) return false;
  std::string target(size, '\0');
  std::ifstream in(path, std::ios::binary);
  if (!in.read(target.data(), static_cast<std::streamsize>(size))) return false;
  if (!fs::exists(path.parent_path() / target, ec)) return false;
  fs::remove(path, ec);
  fs::create_symlink(target, path, ec);
  return !ec;
}
#endif

// Prepend the interpreter's directories to PATH so the `execute` tool's
// _popen/popen resolve `python`. PATH is recomposed from its first-call
// snapshot, so a changed PythonPath needs no removal step; an empty
// `python_dir` restores the snapshot ("system" mode).
bool PrependToPath(const fs::path& python_dir, const log4cxx::LoggerPtr& log) {
#ifdef _WIN32
  static std::optional<std::wstring> base;
  if (python_dir.empty() && !base) return true;
  if (!base) {
    std::wstring current;
    if (DWORD needed = ::GetEnvironmentVariableW(L"PATH", nullptr, 0);
        needed > 0) {
      current.resize(needed);
      current.resize(
          ::GetEnvironmentVariableW(L"PATH", current.data(), needed));
    }
    base = std::move(current);
  }
  const std::wstring prefix =
      python_dir.empty() ? L""
                         : python_dir.wstring() + L';' +
                               (python_dir / "Scripts").wstring() + L';';
  if (!::SetEnvironmentVariableW(L"PATH", (prefix + *base).c_str())) {
    LOG4CXX_ERROR(log, "Failed to update PATH for portable Python (error="
                           << ::GetLastError() << ")");
    return false;
  }
#else
  static std::optional<std::string> base;
  if (python_dir.empty() && !base) return true;
  std::string prefix;
  if (!python_dir.empty()) {
    const fs::path bin = python_dir / "bin";
    std::error_code ec;
    RepairSymlinkStub(bin / "python");
    if (!fs::exists(bin / "python", ec))
      fs::create_symlink("python3", bin / "python", ec);
    // Extraction may also drop the execute bit; fs::permissions follows
    // symlinks, reaching the real interpreter binary.
    fs::permissions(
        bin / "python",
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add, ec);
    prefix = bin.string() + ':';
  }
  if (!base) {
    const char* current = std::getenv("PATH");
    base = current ? current : "";
  }
  if (::setenv("PATH", (prefix + *base).c_str(), /*overwrite=*/1) != 0) {
    LOG4CXX_ERROR(log, "Failed to update PATH for portable Python");
    return false;
  }
#endif
  return true;
}

#endif  // CIL_PORTABLE_PYTHON_SUPPORTED

}  // namespace

std::string GetPortablePythonAssetUrl() { return MLPERF_PORTABLE_PYTHON_URL; }

std::string GetPortablePythonDirName() {
  return fs::path(GetPortablePythonAssetUrl()).stem().string();
}

bool EnsurePortablePythonOnPath(const fs::path& search_dir,
                                const std::string& configured_dir,
                                const log4cxx::LoggerPtr& log) {
#if CIL_PORTABLE_PYTHON_SUPPORTED
  // The GUI can change PythonPath between runs, so re-apply whenever the
  // value differs.
  static std::optional<std::string> applied_dir;
  static bool ok = false;

  if (applied_dir == configured_dir) return ok;
  applied_dir = configured_dir;

  if (configured_dir == "system") {
    ok = PrependToPath({}, log);
    if (ok) LOG4CXX_INFO(log, "Python: system PATH");
    return ok;
  }

  std::optional<fs::path> python_dir =
      FindPythonDir(search_dir, configured_dir);
  if (!python_dir) {
    if (configured_dir.empty())
      LOG4CXX_ERROR(log, "Bundled Python not found");
    else
      LOG4CXX_ERROR(log, "Configured PythonPath has no Python interpreter: "
                             << configured_dir);
    ok = false;
    return false;
  }
  // Make absolute: the `execute` tool `cd`s into the sandbox first.
  fs::path dir = *python_dir;
  std::error_code ec;
  if (fs::path abs = fs::absolute(dir, ec); !ec && !abs.empty())
    dir = std::move(abs);

  ok = PrependToPath(dir, log);
  if (ok) LOG4CXX_INFO(log, "Python enabled: " << dir.string());
  return ok;
#else
  (void)search_dir;
  if (configured_dir.empty()) return true;
  LOG4CXX_ERROR(log,
                "PythonPath is set but Python is not supported on this "
                "platform");
  return false;
#endif
}

}  // namespace cil
