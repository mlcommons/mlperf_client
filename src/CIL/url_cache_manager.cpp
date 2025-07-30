#include "url_cache_manager.h"

#include <log4cxx/logger.h>
#include <openssl/sha.h>

#include <filesystem>
#include <fstream>

#include "../CLI/version.h"
#include "utils.h"

log4cxx::LoggerPtr url_cache_logger(log4cxx::Logger::getLogger("Downloader"));

namespace cil {

const std::string URLCacheManager::kUrlCacheFileName = "url_cache.json";

URLCacheManager::URLCacheManager(const std::string& deps_dir) {
  if (deps_dir.empty()) {
    cache_file_path_ = utils::GetAppDefaultTempPath() / kUrlCacheFileName;
  } else {
    cache_file_path_ = std::filesystem::path(deps_dir) / kUrlCacheFileName;
  }

  LoadCacheFromFile();
}

void URLCacheManager::LoadCacheFromFile() {
  // check if the file exists
  if (std::filesystem::exists(cache_file_path_)) {
    std::ifstream file(cache_file_path_);
    if (file.is_open()) {
      file >> cache_;
      file.close();
    }
  }
}

void URLCacheManager::StoreCacheToFile() {
  std::ofstream file(cache_file_path_);
  if (file.is_open()) {
    file << cache_.dump(4);
    file.close();
  }
}

bool URLCacheManager::IsUrlInCache(const std::string& url) const {
  return cache_.contains(url);
}

bool URLCacheManager::ValidateFile(const std::string& url,
                                   const std::string& filePath,
                                   bool check_version) const {
  bool is_valid = false;
  if (IsUrlInCache(url)) {
    std::string sha256 = utils::ComputeFileSHA256(filePath, url_cache_logger);
    if (!sha256.empty()) {
      is_valid = cache_[url][1] == sha256;
    }
    if (is_valid && check_version) {
      // check if there is 2nd element in the array
      try {
        is_valid = cache_[url][2] == std::string(APP_VERSION_STRING);
      } catch (const std::out_of_range& e) {
        // if there is no 2nd element, it means that the file was cached before
        // the versioning was implemented, so we need to invalidate the cache
        is_valid = false;
      }
    }
  }
  return is_valid;
}

void URLCacheManager::AddUrlToCache(const std::string& url,
                                    const std::string& file_path,
                                    bool with_version) {
  // clean the cache, if the file path exists in other urls, remove it
  for (nlohmann::json::iterator it = cache_.begin(); it != cache_.end();) {
    std::string it_value = it.value()[0];
    if (it_value == file_path) {
      // remove path from the cache
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
  std::string sha256 = utils::ComputeFileSHA256(file_path, url_cache_logger);
  if (!sha256.empty()) {
    cache_[url] = {file_path, sha256,
                   std::string(with_version ? APP_VERSION_STRING : "")};
    StoreCacheToFile();
  }
}

std::string URLCacheManager::GetFilePathFromCache(
    const std::string& url) const {
  return IsUrlInCache(url) ? cache_[url][0] : "";
}

void URLCacheManager::ClearCache() {
  for (const auto& [url, file_info] : cache_.items()) {
    std::filesystem::path file_path = file_info[0];
    if (std::filesystem::exists(file_path)) {
      try {
        std::filesystem::remove(file_path);
      } catch (const std::filesystem::filesystem_error& e) {
        LOG4CXX_ERROR(url_cache_logger, "Failed to remove file: "
                                            << file_path << ", " << e.what());
      }
    }
  }

  cache_.clear();
  if (std::filesystem::exists(cache_file_path_))
    std::filesystem::remove(cache_file_path_);
}

}  // namespace cil