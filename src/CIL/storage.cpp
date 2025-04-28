#include "storage.h"

#include <httplib.h>
#include <log4cxx/logger.h>

#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>

#include "url_cache_manager.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerStorage(Logger::getLogger("Downloader"));

namespace cil {

Storage::Storage(bool force_download)
    : force_download_(force_download), check_version_(false) {}

Storage::Storage(const std::string& storage_path,
                 const std::shared_ptr<URLCacheManager>& url_cache_manager,
                 bool force_download, bool check_version)
    : storage_path_(storage_path),
      url_cache_manager_(url_cache_manager),
      force_download_(force_download),
      check_version_(check_version) {}

Storage::~Storage() = default;

void Storage::SetStoragePath(const std::string& storage_path) {
  storage_path_ = storage_path;
}

const std::string Storage::GetStoragePath() const {
  if (storage_path_.is_absolute()) {
    return storage_path_.string();
  }
  return std::filesystem::absolute(storage_path_).string();
}

bool Storage::FindFile(const std::string& file_name,
                       const DownloadProgressCallback& download_callback,
                       std::string& file_path) {
  return FindFile(file_name, "", download_callback, file_path);
}

/**
 * Extracts the filename from a URI or file path.
 *
 * This function checks if the provided path starts with "file://" or "https://"
 * or "http://". If so, it extracts and returns the filename part of the path.
 * If the path does not start with one of these schemes, the function returns an
 * empty string.
 *
 * @param uri The URI or file path from which to extract the filename.
 * @return The extracted filename if the URI starts with the specified scheme,
 *         otherwise an empty string.
 */
std::string Storage::ExtractFilenameFromURI(const std::string& uri) const {
  std::filesystem::path p;

  if (uri.rfind("file://", 0) == 0) {
    // Remove "file://" scheme
    p = std::filesystem::path(uri.substr(7));
  } else if (uri.rfind("https://", 0) == 0 || uri.rfind("http://", 0) == 0) {
    p = std::filesystem::path(uri);
  }
  // Extract and return the filename part
  return p.filename().string();
}

bool Storage::FindFile(const std::string& file_name, const std::string& sub_dir,
                       const DownloadProgressCallback& download_callback,
                       std::string& file_path) {
  std::filesystem::path subdir_path = storage_path_ / sub_dir;
  if (!std::filesystem::exists(subdir_path)) {
    if (!utils::CreateDirectory(subdir_path.string())) {
      return false;
    }
  }

  std::string actual_file_name = ExtractFilenameFromURI(file_name);
  std::string url_path;
  if (!actual_file_name.empty()) {
    url_path = file_name;
  }
  std::filesystem::path fs_path = subdir_path / actual_file_name;
  return FindFileWithUrl(fs_path, download_callback, file_path, url_path);
}

/**
 * Returns the path if the local file exists, otherwise returns an empty string.
 *
 * This function processes a given URI or file path.
 * If the path starts with "file://",the scheme is removed. The function then
 checks if the resulting local file path
 * exists.
 * For paths starting with "http://" or "https://", the function will extract
 the filename from the URI and check if the file exists locally.

 * @param file_name The URL or file path to process.
 * @return The path to the local file if it exists, otherwise an empty string.
 */
std::string Storage::CheckIfLocalFileExists(const std::string& file_name,
                                            const std::string& sub_dir,
                                            bool ignore_cache,
                                            bool cache_local_files) {
  if (!cache_local_files && file_name.rfind("file://", 0) == 0) {
    auto file_path = std::filesystem::path(file_name.substr(7));
    if (fs::exists(file_path)) {
      return file_path.string();
    }
  }
  std::string actual_file_name = ExtractFilenameFromURI(file_name);
  if (actual_file_name.empty()) {
    return actual_file_name;
  }
  std::filesystem::path subdir_path = storage_path_ / sub_dir;
  std::filesystem::path fs_path = subdir_path / actual_file_name;
  if (std::filesystem::exists(fs_path)) {
    LOG4CXX_INFO(loggerStorage, "The file " << file_name
                                            << " exists locally in "
                                            << fs_path.string() << ".");
    if (ignore_cache) {
      return fs_path.string();
    }
    // check the cache to make sure the file has the same uri
    // in this case the file name contain either the local file path of the url
    // so we need to check if the file name is the same as the file path in the
    // cache and the file is valid by checking the hash
    if (url_cache_manager_->ValidateFile(file_name, fs_path.string(),
                                         check_version_)) {
      LOG4CXX_INFO(loggerStorage,
                   "The file " << file_name
                               << " is valid and match the url in the cache.");
      return fs_path.string();
    } else {
      LOG4CXX_WARN(loggerStorage,
                   "The file " << file_name
                               << " exists but not match the uri in the cache");
    }
  } else {
    LOG4CXX_INFO(loggerStorage,
                 "The file " << file_name << " does not exist locally.");
  }
  return "";
}

bool Storage::FindFileWithUrl(const std::filesystem::path& fs_path,
                              const DownloadProgressCallback& download_callback,
                              std::string& file_path,
                              std::string url_for_download) {
  const auto fs_path_as_string = fs_path.string();

  const bool fs_path_exists = std::filesystem::exists(fs_path);
  const bool url_in_cache =
      url_cache_manager_ &&
      url_cache_manager_->GetFilePathFromCache(url_for_download) ==
          fs_path_as_string &&
      fs::exists(fs_path_as_string) &&
      url_cache_manager_->ValidateFile(url_for_download, fs_path_as_string,
                                       check_version_);

  bool result = fs_path_exists && url_in_cache && !force_download_;
  if (!result) {
    std::string file_url;
    if (!url_for_download.empty()) {
      // Use the provided URL if it's not empty
      file_url = url_for_download;
      // LOG4CXX_INFO(loggerStorage, "Downloading the file "
      //                                 << fs_path.filename()
      //                                 << " from URL: " << file_url);
    } else {
      // If no URL provided, find the file URL
      // LOG4CXX_INFO(
      //    loggerStorage,
      //    "File " << fs_path.filename()
      //            << " was not found. Searching for URL to download from...");
      if (FindFileUrl(fs_path, file_url)) {
        // LOG4CXX_INFO(loggerStorage,
        //              "Found URL to download the file: " << file_url);
      } else {
        // LOG4CXX_ERROR(loggerStorage, "Failed to find URL to download the
        // file: "
        //                                  << fs_path.filename());
        return false;
      }
    }

    downloader_ = std::make_shared<Downloader>(fs_path_as_string);
    std::regex github_regex(R"((?:https?://)?github\.com/(.*)/blob/(.*))");
    std::smatch match;
    if (std::regex_search(file_url, match, github_regex)) {
      if (3 == match.size()) {
        std::string file_path = match[1].str() + "/" + match[2].str();

        std::regex placeholder_file_path_regex("\\{file_path\\}");
        std::string media_github_url =
            "https://media.githubusercontent.com/media/{file_path}";

        media_github_url = std::regex_replace(
            media_github_url, placeholder_file_path_regex, file_path);
        if (!(result = downloader_->operator()(media_github_url,
                                               download_callback))) {
          std::string raw_github_url =
              "https://raw.githubusercontent.com/{file_path}";

          raw_github_url = std::regex_replace(
              raw_github_url, placeholder_file_path_regex, file_path);

          result = downloader_->operator()(raw_github_url, download_callback);
        }
      } else {
        // Invalid github file url
        return false;
      }
    } else {
      result = downloader_->operator()(file_url, download_callback);
    }
  }

  if (result) {
    file_path = fs_path.string();
    if (url_cache_manager_)
      url_cache_manager_->AddUrlToCache(url_for_download, file_path,
                                        check_version_);
  }
  return result;
}

bool Storage::FindFileUrl(const std::filesystem::path& file_path,
                          std::string& file_url) {
  std::string path = utils::GetCurrentDirectory();
  auto expected_file_url_path = std::filesystem::path(path + "/url.local");

  if (!std::filesystem::exists(expected_file_url_path)) {
    // LOG4CXX_ERROR(loggerStorage, "url.local file not found");
    return false;
  }

  std::ifstream inputFile(expected_file_url_path);
  if (!inputFile.is_open()) {
    // LOG4CXX_ERROR(loggerStorage,
    //               "Failed to open the file: " << expected_file_url_path);
    return false;
  }

  std::string line;
  while (std::getline(inputFile, line)) {
    std::filesystem::path line_path = line;

    // Check if line is a URL
    std::regex url_regex(R"(^https?://)");
    if (std::regex_search(line, url_regex)) {
      // Extract the filename from the URL
      if (line_path.filename() == file_path.filename()) {
        file_url = line;
        return true;
      }
    } else {
      // Convert relative path to absolute path
      if (line_path.is_relative()) {
        line_path = std::filesystem::absolute(line_path);
      }

      // Check if the line contains the filename or if it's a directory
      if (line_path.filename() == file_path.filename()) {
        file_url = line_path.string();
        return true;
      } else if (std::filesystem::is_directory(line_path)) {
        std::filesystem::path potential_file = line_path / file_path.filename();
        if (std::filesystem::exists(potential_file)) {
          file_url = potential_file.string();
          return true;
        }
      }
    }
  }

  // LOG4CXX_ERROR(loggerStorage,
  //               "Failed to find URL or file path in url.local for the file: "
  //                   << file_path.filename());
  return false;
}

void Storage::StopDownload() {
  if (downloader_) {
    downloader_->StopDownload();
  }
}
}  // namespace cil
