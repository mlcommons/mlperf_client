#ifndef URLCACHEMANAGER_H
#define URLCACHEMANAGER_H

#include <nlohmann/json.hpp>
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
                    bool check_version = false) const;
  void AddUrlToCache(const std::string& url, const std::string& file_path,
                     bool with_version = false);
  std::string GetFilePathFromCache(const std::string& url) const;

  void ClearCache();

 private:
  static const std::string kUrlCacheFileName;

  std::filesystem::path cache_file_path_;
  nlohmann::json cache_;

  void LoadCacheFromFile();
  void StoreCacheToFile();
};

}  // namespace cil

#endif  // URLCACHEMANAGER_H