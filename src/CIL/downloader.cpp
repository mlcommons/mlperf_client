#include "downloader.h"

#include <httplib.h>
#include <log4cxx/logger.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <regex>

#include "progress_notifier.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerDownloader(Logger::getLogger("Downloader"));

namespace cil {

bool HasFileScheme(const std::string& url) {
  return std::regex_search(url, std::regex(R"(^file://)"));
}

bool HasHttpScheme(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

// Template function to set proxy if available
template <typename ClientType>
void Downloader::SetProxyIfAvailable(ClientType& client) const {
  const char* http_proxy = std::getenv("HTTP_PROXY");
  const char* https_proxy = std::getenv("HTTPS_PROXY");

  if (http_proxy) {
    const auto [host, port] = ParseProxy(http_proxy);
    client.set_proxy(host.c_str(), port);
  }
  if (https_proxy) {
    auto [host, port] = ParseProxy(https_proxy);
    client.set_proxy(host.c_str(), port);
  }

  const char* proxy_user = std::getenv("PROXY_USER");
  const char* proxy_pass = std::getenv("PROXY_PASS");

  if (proxy_user && proxy_pass) {
    client.set_proxy_basic_auth(proxy_user, proxy_pass);
  }

  if (http_proxy || https_proxy) {
    LOG4CXX_INFO(loggerDownloader,
                 "http_proxy: " << http_proxy
                                << " | https_proxy: " << https_proxy
                                << " | proxy_user: " << proxy_user);
  }
}

// Function to parse proxy URL
std::pair<std::string, int> Downloader::ParseProxy(
    const std::string_view& proxy) const {
  std::string host;
  int port = 8080;
  size_t pos = proxy.find("://");
  if (pos != std::string::npos) {
    host = proxy.substr(pos + 3);
  } else {
    host = proxy;
  }

  pos = host.find(':');
  if (pos != std::string::npos) {
    port = std::stoi(host.substr(pos + 1));
    host = host.substr(0, pos);
  }
  return std::make_pair(host, port);
}

template <typename ClientType>
Expected<RemoteFileMeta> Downloader::GetRemoteFileMeta(
    ClientType& client, const std::string& host_file_path) const {
  auto res = client.Head(host_file_path.c_str());
  if (!res) {
    // No response at all (DNS / connect / TLS / timeout): treat as offline.
    LOG4CXX_ERROR(loggerDownloader,
                  "Error getting the content length of the file: "
                      << host_file_path << " - no response (" << res.error()
                      << ")");
    return Expected<RemoteFileMeta>::error(
        "could not connect to the download server - check your internet "
        "connection");
  }
  if (res->status != 200) {
    LOG4CXX_ERROR(loggerDownloader,
                  "Error getting the content length of the file: "
                      << host_file_path << " - HTTP status " << res->status);
    return Expected<RemoteFileMeta>::error(
        "the download server returned HTTP status " +
        std::to_string(res->status) +
        " (the file may be unavailable or have moved)");
  }

  auto content_length = res->get_header_value("Content-Length");
  if (content_length.empty()) {
    LOG4CXX_ERROR(loggerDownloader,
                  "Error getting the content length of the file: "
                      << host_file_path << " - missing Content-Length header");
    return Expected<RemoteFileMeta>::error(
        "the download server did not report the file size");
  }

  auto accept_range_it = res->headers.find("Accept-Ranges");
  return RemoteFileMeta{std::stoull(content_length),
                        accept_range_it != res->headers.end() &&
                            accept_range_it->second == "bytes"};
}

void Downloader::DownloadChunk(const std::string& host_name,
                               const std::string& host_file_path,
                               uint64_t start, uint64_t end,
                               const std::string& output_file_path,
                               std::atomic<uint64_t>& progress,
                               std::atomic<bool>& has_failed,
                               int retry = 3) const {
  // Retry if the download fails
  for (int i = 0; i < retry; ++i) {
    // Fail fast if the download has already failed
    if (has_failed) {
      return;
    }
    httplib::Client client(host_name);
    SetProxyIfAvailable(client);
    client.set_connection_timeout(60, 0);
    client.set_read_timeout(60, 0);
    httplib::Headers headers = {{"Range", "bytes=" + std::to_string(start) +
                                              "-" + std::to_string(end)}};
    // Open the pre-allocated destination without truncating so sibling
    // threads' writes to other byte ranges are preserved, then seek to this
    // chunk's start offset. The ios::in flag is what suppresses truncation.
    std::fstream output_file(output_file_path,
                             std::ios::binary | std::ios::in | std::ios::out);
    if (!output_file) {
      LOG4CXX_ERROR(loggerDownloader,
                    "Error opening destination file for chunk ("
                        << start << " - " << end << "): " << output_file_path);
      if (i < retry - 1) continue;
      break;
    }
    output_file.seekp(start);
    uint64_t downloaded_size = 0;
    bool disk_full = false;
    auto res = client.Get(
        host_file_path.c_str(), headers,
        [&](const char* data, size_t data_length) -> bool {
          if (stop_download.load()) {
            LOG4CXX_ERROR(loggerDownloader, "Download stopped by user.");
            return false;
          }
          output_file.write(data, data_length);
          if (output_file.fail()) {
            if (errno == ENOSPC) {
              disk_full = true;
              LOG4CXX_ERROR(loggerDownloader,
                            "Error: Not enough disk space to write "
                            "chunk ("
                                << start << " - " << end << ") of "
                                << host_file_path);
            } else {
              LOG4CXX_ERROR(loggerDownloader, "Error writing chunk ("
                                                  << start << " - " << end
                                                  << ") of " << host_file_path);
            }
            return false;
          }
          downloaded_size += data_length;
          progress += data_length;

          return true;
        });

    if (!res || res->status != 206) {
      // Revert progress
      progress -= downloaded_size;
      LOG4CXX_ERROR(loggerDownloader, "Error downloading chunk ("
                                          << start << " - " << end << ") of "
                                          << host_file_path);
      if (res) {
        auto error = res.error();
        LOG4CXX_ERROR(loggerDownloader, "Status code: " << res->status);
        LOG4CXX_ERROR(loggerDownloader, "Error: " << error);
      }

      if (disk_full) break;

      if (i < retry - 1) {
        LOG4CXX_ERROR(loggerDownloader, "Retrying download chunk ("
                                            << start << " - " << end << ") of "
                                            << host_file_path);
        continue;
      }
      LOG4CXX_ERROR(loggerDownloader, "Failed to download chunk ("
                                          << start << " - " << end << ") of "
                                          << host_file_path);
    }
    output_file.close();
    return;
  }
  has_failed = true;
}

bool Downloader::DownloadRemoteFile(
    const std::string& host_name, const std::string& host_file_path,
    const std::string& destination_file_path, int num_threads,
    const DownloadProgressCallback& progress_callback) const {
  httplib::Client client(host_name);
  SetProxyIfAvailable(client);
  client.set_follow_location(true);

  auto meta = GetRemoteFileMeta(client, host_file_path);
  if (!meta) return false;
  const uint64_t file_size = meta.value().size;
  const bool accepts_range = meta.value().accepts_range;

  // Small file optimization or fallback for unsupported range requests:
  // If the server does not support range requests, or if threads num is 1, or
  // the file size is less than 1MB, download the file without splitting it

  ProgressNotifier progress_notifier;
  if (!accepts_range || num_threads == 1 ||
      num_threads > 1 && file_size < 1024 * 1024) {
    std::ofstream output_file(destination_file_path, std::ios::binary);
    uint64_t downloaded_size = 0;
    auto res = client.Get(
        host_file_path.c_str(),
        [&](const char* data, size_t data_length) -> bool {
          output_file.write(data, data_length);
          if (output_file.fail()) {
            if (errno == ENOSPC) {
              LOG4CXX_ERROR(loggerDownloader,
                            "Error: Not enough disk space to "
                            "download the file: "
                                << host_file_path);
            } else {
              LOG4CXX_ERROR(loggerDownloader,
                            "Error writing the file: " << host_file_path);
            }
            return false;
          }
          downloaded_size += data_length;
          progress_notifier((int)(downloaded_size * 100 / file_size),
                            downloaded_size, file_size, progress_callback);
          return true;
        });
    output_file.close();
    if (!res || res->status != 200) {
      LOG4CXX_ERROR(loggerDownloader,
                    "Error downloading the file: " << host_file_path);
      return false;
    }
    return true;
  }

  // Pre-allocate the destination file so each chunk thread can seek to its
  // start offset and write directly into the final file in parallel — no
  // per-chunk .part files and no separate merge phase. std::filesystem::
  // resize_file is C++17 and portable across Windows / Linux / macOS, but it
  // requires the file to exist, so create it first.
  {
    std::ofstream create_file(destination_file_path,
                              std::ios::binary | std::ios::trunc);
    if (!create_file) {
      LOG4CXX_ERROR(loggerDownloader, "Error creating destination file: "
                                          << destination_file_path);
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::resize_file(destination_file_path, file_size, ec);
  if (ec) {
    LOG4CXX_ERROR(loggerDownloader, "Error preallocating destination file ("
                                        << destination_file_path
                                        << "): " << ec.message());
    return false;
  }

  std::vector<std::thread> threads;
  std::atomic<uint64_t> progress(0);
  uint64_t chunk_size = file_size / num_threads;
  std::atomic<bool> has_failed = false;
  for (int i = 0; i < num_threads; ++i) {
    uint64_t start = i * chunk_size;
    uint64_t end =
        (i == num_threads - 1) ? file_size - 1 : (start + chunk_size - 1);
    threads.emplace_back(&Downloader::DownloadChunk, this, host_name,
                         host_file_path, start, end, destination_file_path,
                         std::ref(progress), std::ref(has_failed), 5);
  }

  std::thread progress_thread([&]() {
    while (progress < file_size && !has_failed && !stop_download.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      progress_notifier(static_cast<int>(static_cast<double>(progress) * 100 /
                                         static_cast<double>(file_size)),
                        progress.load(), file_size, progress_callback);
    }
  });

  for (auto& thread : threads) {
    if (thread.joinable()) thread.join();
  }
  if (progress_thread.joinable()) progress_thread.join();

  if (has_failed || stop_download.load()) {
    LOG4CXX_ERROR(loggerDownloader,
                  "Error downloading the file: " << host_file_path);
    return false;
  }

  progress_notifier(100, file_size, file_size, progress_callback);
  return true;
}

Expected<uint64_t> Downloader::GetRemoteFileSize(
    const std::string& file_url) const {
  if (HasFileScheme(file_url) || !HasHttpScheme(file_url)) return uint64_t{0};

  // Parse the URL to extract the host name and file path
  std::string host_name;
  std::string host_file_path;
  ParseFileUrl(file_url, host_name, host_file_path);

  httplib::Client client(host_name);
  SetProxyIfAvailable(client);
  client.set_follow_location(true);

  auto meta = GetRemoteFileMeta(client, host_file_path);
  if (!meta) return Expected<uint64_t>::error(meta.error());
  return meta.value().size;
}

Downloader::Downloader(const std::string& destination_file_path)
    : destination_file_path_(destination_file_path) {}

Downloader::~Downloader() = default;

bool Downloader::operator()(const std::string& file_url,
                            bool get_file_size_only, bool include_local_size,
                            uint64_t& file_size,
                            const DownloadProgressCallback& progress_callback,
                            std::string& error_message) const {
  if (get_file_size_only) {
    if (include_local_size &&
        (HasFileScheme(file_url) || !HasHttpScheme(file_url))) {
      const std::string local_path =
          HasFileScheme(file_url) ? file_url.substr(7) : file_url;
      std::error_code ec;
      const auto size = std::filesystem::file_size(local_path, ec);
      if (ec) {
        LOG4CXX_ERROR(loggerDownloader,
                      "Failed to stat local file: " << local_path << " ("
                                                    << ec.message() << ")");
        error_message = "could not read the local file (" + ec.message() + ")";
        return false;
      }
      file_size = static_cast<uint64_t>(size);
      return true;
    }
    auto remote_size = GetRemoteFileSize(file_url);
    if (!remote_size) {
      error_message = remote_size.error();
      return false;
    }
    file_size = remote_size.value();
    return true;
  }

  if (HasFileScheme(file_url))
    return CopyFileWithProgress(file_url.substr(7), destination_file_path_,
                                progress_callback);
  if (!HasHttpScheme(file_url))
    return CopyFileWithProgress(file_url, destination_file_path_,
                                progress_callback);

  // Parse the URL to extract the host name and file path
  std::string host_name;
  std::string host_file_path;
  auto url_parts = ParseFileUrl(file_url, host_name, host_file_path);

  // Log some information about the download
  LOG4CXX_INFO(
      loggerDownloader,
      "Downloading file: " << file_url << " to " << destination_file_path_);

  std::string temp_download_path = destination_file_path_ + ".tmp";

  const unsigned max_num_threads = 4u;
  unsigned num_threads =
      std::min(std::max(std::thread::hardware_concurrency(), 1u),
               (unsigned int)max_num_threads);

  bool download_success =
      DownloadRemoteFile(host_name, host_file_path, temp_download_path,
                         num_threads, progress_callback);
  if (!download_success) {
    // remove the partially-written temp file
    std::remove(temp_download_path.c_str());
    LOG4CXX_ERROR(loggerDownloader, "Error downloading file: " << file_url);
  } else {
    // rename the temp file to the destination file
    try {
      std::filesystem::rename(temp_download_path, destination_file_path_);
      LOG4CXX_INFO(loggerDownloader,
                   "Downloaded file: " << file_url << " successfully to "
                                       << destination_file_path_);
    } catch (const std::exception& e) {
      LOG4CXX_ERROR(loggerDownloader, "Error renaming the temp file: "
                                          << temp_download_path << " to "
                                          << destination_file_path_
                                          << " Error: " << e.what());
      download_success = false;
    }
  }
  return download_success;
}

// can be moved to utils
bool Downloader::CopyFileWithProgress(
    const std::string& source_path, const std::string& destination_path,
    const DownloadProgressCallback& progress_callback) {
  std::filesystem::path src_path(source_path);
  if (src_path.is_relative()) {
    // Append the current working directory to the source path
    src_path = utils::GetCurrentDirectory() / src_path;
  }
  // Check if the source file exists and is a regular file
  if (!std::filesystem::exists(src_path) ||
      !std::filesystem::is_regular_file(src_path)) {
    LOG4CXX_ERROR(loggerDownloader,
                  "Error: Source file does not exist or is not a regular file: "
                      << source_path);
    return false;
  }
  std::ifstream source_file(src_path.c_str(), std::ios::binary);
  if (!source_file) {
    LOG4CXX_ERROR(loggerDownloader,
                  "Error opening source file: " << source_path);
    return false;
  }

  std::ofstream destination_file(destination_path,
                                 std::ios::binary | std::ios::trunc);
  if (!destination_file) {
    LOG4CXX_ERROR(loggerDownloader, "Error opening/creating destination file: "
                                        << destination_path);
    return false;
  }

  source_file.seekg(0, std::ios::end);
  std::streamsize file_size = source_file.tellg();
  source_file.seekg(0, std::ios::beg);

  const std::streamsize kBufferSize = 4096;
  std::vector<char> buffer(kBufferSize);

  std::streamsize bytes_copied = 0;

  ProgressNotifier progress_notifier;
  while (!source_file.eof() && bytes_copied < file_size) {
    source_file.read(buffer.data(), kBufferSize);
    destination_file.write(buffer.data(), source_file.gcount());

    bytes_copied += source_file.gcount();

    progress_notifier((int)(bytes_copied * 100 / file_size),
                      (uint64_t)bytes_copied, (uint64_t)file_size,
                      progress_callback);
  }

  if (!destination_file) {
    LOG4CXX_ERROR(loggerDownloader,
                  "Error copying contents of the file: " << source_path);
    return false;
  }
  return true;
}

bool Downloader::ParseFileUrl(const std::string& file_url,
                              std::string& host_name,
                              std::string& host_file_path) {
  // Check for the protocol (http://, https://, or ftp://)
  size_t protocol_end = file_url.find("://");

  // Start position for searching the next slash
  size_t start_pos = (protocol_end != std::string::npos) ? protocol_end + 3 : 0;

  // Find the next slash, which indicates the start of the file path
  size_t pos = file_url.find('/', start_pos);
  if (pos != std::string::npos) {
    // Extract host name and file path
    host_name = file_url.substr(start_pos, pos - start_pos);
    host_file_path = file_url.substr(pos + 1);
  }

  if (!host_file_path.empty() && host_file_path[0] != '/')
    host_file_path = "/" + host_file_path;

  return true;
}

void Downloader::StopDownload() { stop_download.store(true); }

}  // namespace cil
