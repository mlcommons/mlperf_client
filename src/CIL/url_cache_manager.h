#ifndef URLCACHEMANAGER_H
#define URLCACHEMANAGER_H

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace cil {

class URLCacheManager {
 public:
  explicit URLCacheManager(const std::string& deps_dir);
  ~URLCacheManager() = default;

  URLCacheManager(const URLCacheManager&) = delete;
  URLCacheManager& operator=(const URLCacheManager&) = delete;
  URLCacheManager(URLCacheManager&&) = delete;
  URLCacheManager& operator=(URLCacheManager&&) = delete;

  bool IsUrlInCache(const std::string& url) const;
  bool ValidateFile(const std::string& url, const std::string& filePath,
                    bool check_version = false, bool forced = false) const;
  void AddUrlToCache(const std::string& url, const std::string& file_path,
                     bool with_version = false);
  std::string GetFilePathFromCache(const std::string& url) const;

  void ClearCache();

  /**
   * @brief Process-level URL→bytes-needed cache. `0` = locally cached, `>0` =
   *        bytes still to fetch, absent = unknown. Shared across every
   *        URLCacheManager instance so a file referenced by N EP configs is
   *        HEAD'd at most once per launch and the main pass can skip
   *        creating download tasks for files already known cached.
   */
  static std::optional<uint64_t> GetCachedSize(const std::string& url);
  static void SetCachedSize(const std::string& url, uint64_t bytes_needed);

 private:
  static const std::string kUrlCacheFileName;

  std::filesystem::path cache_file_path_;
  nlohmann::json cache_;

  void LoadCacheFromFile();
  void StoreCacheToFile();
};

}  // namespace cil

#endif  // URLCACHEMANAGER_H