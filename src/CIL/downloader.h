#pragma once

#include <atomic>
#include <functional>
#include <string>

#if RELEASE_BUILD
#define LOCAL_DOWNLOAD 0
#else
#define LOCAL_DOWNLOAD 0
#endif

namespace cil {

typedef std::function<void(int)> DownloadProgressCallback;
/**
 * @class Downloader
 *
 * @brief This class is responsible for downloading files from local or remote
 * sources to a destination file path.
 *
 * The supported files should be either local files and the file URL should be
 * in the format of "file://<path>" or remote files and the file URL should be
 * in the format of "http://<host>/<path>" or "https://<host>/<path>".
 */
class Downloader {
 public:
  /**
   * @brief Constructor for Downloader class.
   *
   * @param destination_file_path The path to the destination file.
   *
   */
  Downloader(const std::string& destination_file_path);
  ~Downloader();

 public:
  /**
   * @brief Downloads the file from the specified URL to the destination file
   * path.
   *
   * This method downloads the file from the specified URL to the destination
   * file path.
   *
   * @param file_url The URL of the file to download (either local or remote) in
   * the format of "file://<path>" for local files or "http://<host>/<path>" or
   * "https://<host>/<path>" for remote files.
   * @param progress_callback The callback function to report the download
   * progress.
   * @return True if the file is downloaded successfully, false otherwise.
   *
   */

  bool operator()(const std::string& file_url,
                  const DownloadProgressCallback& progress_callback) const;
  /**
   * @brief Interrupts the download process.
   *
   * This method interrupts the download process and stops the download.
   */
  void StopDownload();

 private:
  static bool CopyFileWithProgress(
      const std::string& source_path, const std::string& destination_path,
      const DownloadProgressCallback& progress_callback);

  static bool ParseFileUrl(const std::string& file_url, std::string& host_name,
                           std::string& host_file_path);

 private:
  std::string destination_file_path_;
  std::atomic<bool> stop_download;

  bool DownloadRemoteFile(
      const std::string& host_name, const std::string& host_file_path,
      const std::string& destination_file_path, int num_threads,
      const DownloadProgressCallback& progress_callback) const;
  void DownloadChunk(const std::string& host_name,
                     const std::string& host_file_path, uint64_t start,
                     uint64_t end, const std::string& output_file_path,
                     std::atomic<uint64_t>& progress,
                     std::atomic<bool>& has_failed, int retry) const;

  template <typename ClientType>
  void SetProxyIfAvailable(ClientType& client) const;
  std::pair<std::string, int> ParseProxy(const std::string_view& proxy) const;
};

}  // namespace cil
