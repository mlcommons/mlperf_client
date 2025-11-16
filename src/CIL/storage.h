#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>

#include "downloader.h"

namespace cil {

class URLCacheManager;

class Storage {
 public:
  Storage(const std::string& storage_path,
          const std::shared_ptr<URLCacheManager>& url_cache_manager,
          bool force_download, bool check_version,
          bool collect_remote_sizes_only);
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
                                     bool ignore_cache, bool cache_local_files);
  void StopDownload();
  bool IsForceDownloadEnabled() const { return force_download_; }
  bool IsCollectRemoteSizesOnlyEnabled() const {
    return collect_remote_sizes_only_;
  }

  const std::map<std::string, uint64_t>& GetRemoteFileSizes() const {
    return remote_file_sizes_;
  };

 private:
  bool FindFileWithUrl(const std::filesystem::path& fs_path,
                       const DownloadProgressCallback& download_callback,
                       std::string& file_path, std::string url_for_download);
  static bool FindFileUrl(const std::filesystem::path& file_path,
                          std::string& file_url);

 private:
  std::filesystem::path storage_path_;
  const bool force_download_;
  const bool collect_remote_sizes_only_;
  const bool check_version_;

  std::map<std::string, uint64_t> remote_file_sizes_;

  std::shared_ptr<URLCacheManager> url_cache_manager_;
  std::shared_ptr<Downloader> downloader_;
};

}  // namespace cil
