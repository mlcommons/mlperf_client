#include "downloader.h"

#include <httplib.h>
#include <log4cxx/logger.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <regex>

#include "progress_notifier.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerDownloader(Logger::getLogger("Downloader"));

namespace cil {

bool CheckDiskSpace(const std::string& destination_path, uint64_t file_size) {
  // Get Parent directory of the destination file
  std::filesystem::path destination_parent_path =
      std::filesystem::path(destination_path).parent_path();
  // make sure the parent directory exists
  if (!std::filesystem::exists(destination_parent_path)) {
    if (!utils::CreateDirectory(destination_parent_path.string())) {
      LOG4CXX_ERROR(loggerDownloader, "Error creating the parent directory: "
                                          << destination_parent_path);
      return false;
    }
  }
  auto available_space =
      utils::GetAvailableDiskSpace(destination_parent_path.string());
  if (available_space < file_size) {
    LOG4CXX_ERROR(
        loggerDownloader,
        "Error: Not enough space to download the file: " << destination_path);
    return false;
  }

  return true;
}

void Downloader::download_chunk(const std::string& host_name,
                    const std::string& host_file_path, uint64_t start,
                    uint64_t end, const std::string& output_file_path,
                    std::atomic<uint64_t>& progress,
                    std::atomic<bool>& has_failed, int retry = 3) {
  // Retry if the download fails
  for (int i = 0; i < retry; ++i) {
    // Fail fast if the download has already failed
    if (has_failed) {
      return;
    }
    httplib::Client client(host_name);
    client.set_connection_timeout(60, 0);
    client.set_read_timeout(60, 0);
    httplib::Headers headers = {{"Range", "bytes=" + std::to_string(start) +
                                              "-" + std::to_string(end)}};
    std::ofstream output_file(output_file_path, std::ios::binary);
    uint64_t downloaded_size = 0;
    auto res = client.Get(host_file_path.c_str(), headers,
                          [&](const char* data, size_t data_length) -> bool {
                            if(stop_download.load()) {
                              LOG4CXX_ERROR(loggerDownloader, "Download stopped by user.");
                              return false;
                            }
                            output_file.write(data, data_length);
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

bool Downloader::download_remote_file(const std::string& host_name,
                          const std::string& host_file_path,
                          const std::string& destination_file_path,
                          int num_threads,
                          const DownloadProgressCallback& progress_callback) {
  httplib::Client client(host_name);
  client.set_follow_location(true);
  ProgressNotifier progress_notifier;
  // Send request to get the content length of the file
  auto res = client.Head(host_file_path.c_str());
  if (!res || res->status != 200) {
    LOG4CXX_ERROR(
        loggerDownloader,
        "Error getting the content length of the file: " << host_file_path);
    if (res) {
      auto error = res.error();
      LOG4CXX_ERROR(loggerDownloader, "Status code: " << res->status);
      LOG4CXX_ERROR(loggerDownloader, "Error: " << error);
    }
    return false;
  }

  auto content_length = res->get_header_value("Content-Length");
  if (content_length.empty()) {
    LOG4CXX_ERROR(
        loggerDownloader,
        "Error getting the content length of the file: " << host_file_path);
    return false;
  }
  uint64_t file_size = std::stoull(content_length);
  // Check if there is enough space to download the file
  if (!CheckDiskSpace(destination_file_path, file_size)) {
    return false;
  }

  // Small file optimization, for files smaller than 1MB
  // If threads num is 1, or the file size is less than 1MB, download the file
  // without splitting it
  if (num_threads == 1 || num_threads > 1 && file_size < 1024 * 1024) {
    std::ofstream output_file(destination_file_path, std::ios::binary);
    auto res =
        client.Get(host_file_path.c_str(),
                   [&](const char* data, size_t data_length) -> bool {
                     output_file.write(data, data_length);
                     progress_notifier((int)(data_length * 100 / file_size),
                                       progress_callback);
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

  std::vector<std::thread> threads;
  std::atomic<uint64_t> progress(0);
  uint64_t chunk_size = file_size / num_threads;
  std::atomic<bool> has_failed = false;
  for (int i = 0; i < num_threads; ++i) {
    uint64_t start = i * chunk_size;
    uint64_t end =
        (i == num_threads - 1) ? file_size - 1 : (start + chunk_size - 1);
    std::string temp_file = destination_file_path + ".part" + std::to_string(i);
    threads.emplace_back(&Downloader::download_chunk,this, host_name, host_file_path, start, end,
                         temp_file, std::ref(progress), std::ref(has_failed),
                         5);
  }

  const double download_progress_interval = 1.0 - num_threads * 0.02;
  const double merge_progress_interval = 1.0 - download_progress_interval;

  double download_progress_percent = 0.0;

  std::thread progress_thread([&]() {
    while (progress < file_size && !has_failed && !stop_download.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      download_progress_percent =
          download_progress_interval * (double)progress * 100 / file_size;

      progress_notifier((int)download_progress_percent, progress_callback);
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

  // merge the chunks
  std::ofstream full_output_file(destination_file_path, std::ios::binary);
  for (int i = 0; i < num_threads; ++i) {
    std::string temp_file_path =
        destination_file_path + ".part" + std::to_string(i);
    if (!std::filesystem::exists(temp_file_path)) {
      LOG4CXX_ERROR(loggerDownloader,
                    "Error: " << temp_file_path << " does not exist.");
      return false;
    }
    try {
      std::ifstream temp_file(temp_file_path, std::ios::binary);
      full_output_file << temp_file.rdbuf();
      temp_file.close();
      std::filesystem::remove(temp_file_path);
    } catch (...) {
      LOG4CXX_ERROR(loggerDownloader, "Error merging the chunks.");
      return false;
    }

    double progress_percent =
        download_progress_percent +
        merge_progress_interval * (double)(i + 1) * 100 / num_threads;

    progress_notifier((int)progress_percent, progress_callback);
  }
  full_output_file.close();
  return true;
}
Downloader::Downloader(const std::string& destination_file_path)
    : destination_file_path_(destination_file_path) {}

Downloader::~Downloader() = default;

bool Downloader::operator()(const std::string& file_url,
                            const DownloadProgressCallback& progress_callback) {
  std::regex file_url_regex(R"(^file://)");
  if (std::regex_search(file_url, file_url_regex)) {
    return CopyFileWithProgress(file_url.substr(7), destination_file_path_,
                                progress_callback);
  }

  if (file_url.rfind("http://", 0) != 0 && file_url.rfind("https://", 0) != 0) {
    return CopyFileWithProgress(file_url, destination_file_path_,
                                progress_callback);
  }

  // Parse the URL to extract the host name and file path
  std::string host_name;
  std::string host_file_path;
  auto url_parts = ParseFileUrl(file_url, host_name, host_file_path);
  //
  if (!host_file_path.empty() && host_file_path[0] != '/') {
    host_file_path = "/" + host_file_path;
  }
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
      download_remote_file(host_name, host_file_path, temp_download_path,
                           num_threads, progress_callback);
  if (!download_success) {
    // remove the temp file if exists
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
  auto src_file_size = fs::file_size(fs::path(source_path));
  if (!CheckDiskSpace(destination_path, src_file_size)) {
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

    progress_notifier((int)(bytes_copied * 100 / file_size), progress_callback);
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

  return true;
}

void Downloader::StopDownload() {
  stop_download.store(true);
}

}  // namespace cil
