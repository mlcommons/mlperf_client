#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "downloader.h"

namespace cil {

class URLCacheManager;

class Storage {
 public:
  Storage(bool force_download = false);
  Storage(const std::string& storage_path,
          const std::shared_ptr<URLCacheManager>& url_cache_manager,
          bool force_download = false, bool check_version = false);
  ~Storage();

 public:
  void SetStoragePath(const std::string& storage_path);
  const std::string GetStoragePath() const;

  bool FindFile(const std::string& file_name,
                const DownloadProgressCallback& download_callback,
                std::string& file_path);

  std::string ExtractFilenameFromURI(const std::string& uri) const;
  bool FindFile(const std::string& file_name, const std::string& sub_dir,
                const DownloadProgressCallback& download_callback,
                std::string& file_path);

  std::string CheckIfLocalFileExists(const std::string& file_name,
                                     const std::string& sub_dir,
                                     bool ignore_cache,
                                     bool cache_local_files);
  void StopDownload();
  bool IsForceDownloadEnabled() const { return force_download_; }

 private:
  bool FindFileWithUrl(const std::filesystem::path& fs_path,
                       const DownloadProgressCallback& download_callback,
                       std::string& file_path, std::string url_for_download);
  static bool FindFileUrl(const std::filesystem::path& file_path,
                          std::string& file_url);

 private:
  std::filesystem::path storage_path_;
  const bool force_download_;
  const bool check_version_;

  std::shared_ptr<URLCacheManager> url_cache_manager_;
  std::shared_ptr<Downloader> downloader_;
};

}  // namespace cil
