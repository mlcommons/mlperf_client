#include "disk_space_tracker.h"

#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/mount.h>
#else
#include <mntent.h>
#endif

#include "utils.h"

namespace cil {

namespace {

fs::path FirstExistingAncestor(const fs::path& p) {
  std::error_code ec;
  fs::path current = p.parent_path();
  while (!current.empty()) {
    if (fs::exists(current, ec)) return current;
    const fs::path parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
  return {};
}

fs::path MountPointOf(const fs::path& existing_path) {
#if defined(_WIN32) || defined(_WIN64)
  if (std::vector<wchar_t> volume(MAX_PATH);
      GetVolumePathNameW(existing_path.c_str(), volume.data(), MAX_PATH))
    return fs::path(volume.data());
#elif defined(__APPLE__)
  struct statfs buf;
  if (::statfs(existing_path.c_str(), &buf) == 0) {
    return fs::path(buf.f_mntonname);
  }
#else  // Linux
  const std::string path = existing_path.string();
  FILE* mtab = ::setmntent("/proc/mounts", "r");
  if (mtab) {
    struct mntent* entry;
    fs::path best = "/";
    size_t best_len = 1;

    while ((entry = ::getmntent(mtab)) != nullptr) {
      const std::string mp = entry->mnt_dir;
      if (mp == "/") continue;
      if ((path == mp || path.starts_with(mp + '/')) && mp.size() > best_len) {
        best = mp;
        best_len = mp.size();
      }
    }
    ::endmntent(mtab);
    return best;
  }
#endif
  return existing_path;
}

}  // namespace

fs::path DiskSpaceTracker::VolumeOf(const fs::path& dest_path) {
  std::error_code ec;
  fs::path probe = fs::weakly_canonical(dest_path, ec);
  if (ec) probe = dest_path;
  const fs::path ancestor = FirstExistingAncestor(probe);
  if (ancestor.empty()) return probe.root_path();
  return MountPointOf(ancestor);
}

void DiskSpaceTracker::RegisterPlannedDownload(const fs::path& dest_path,
                                               uint64_t size) {
  if (dest_path.empty() || size == 0) return;

  const std::string file_key = dest_path.string();
  if (files_.contains(file_key)) return;
  files_[file_key] = size;

  fs::path probe = VolumeOf(dest_path);
  auto& bucket = volumes_[probe.string()];
  if (bucket.path.empty()) bucket.path = probe;
  bucket.bytes += size;
  if (size > bucket.largest) bucket.largest = size;
}

bool DiskSpaceTracker::HasEnoughSpace(const log4cxx::LoggerPtr& logger,
                                      const std::string& context,
                                      std::string* failure_message) const {
  bool enough = true;
  std::stringstream summary;
  for (const auto& [key, bucket] : volumes_) {
    if (bucket.bytes == 0) continue;
    // Reserve merge scratch for the chunked downloader (~half the largest
    // file).
    const uint64_t headroom = bucket.largest / 2;
    const uint64_t required = bucket.bytes + headroom;
    std::error_code ec;
    const auto info = fs::space(bucket.path, ec);
    const uint64_t available = ec ? 0 : static_cast<uint64_t>(info.available);
    if (required > available) {
      auto gb_required =
          utils::DoubleToFixedString(utils::BytesToGb(required), 2);
      auto gb_payload =
          utils::DoubleToFixedString(utils::BytesToGb(bucket.bytes), 2);
      auto gb_headroom =
          utils::DoubleToFixedString(utils::BytesToGb(headroom), 2);
      auto gb_available =
          utils::DoubleToFixedString(utils::BytesToGb(available), 2);
      LOG4CXX_ERROR(logger, context << ": volume " << bucket.path.string()
                                    << " needs " << gb_required << " GB ("
                                    << gb_payload << " GB + " << gb_headroom
                                    << " GB working space), " << gb_available
                                    << " GB available");
      if (failure_message) {
        if (summary.tellp() > 0) summary << "\n";
        summary << "Volume " << bucket.path.string() << " needs " << gb_required
                << " GB but only " << gb_available << " GB is available.";
      }
      enough = false;
    }
  }
  if (failure_message && !enough) *failure_message = summary.str();
  return enough;
}

void DiskSpaceTracker::MarkCompleted(const fs::path& dest_path) {
  if (dest_path.empty()) return;

  const std::string file_key = dest_path.string();
  auto it = files_.find(file_key);
  if (it == files_.end()) return;

  const uint64_t size = it->second;
  files_.erase(it);

  fs::path probe = VolumeOf(dest_path);
  auto bucket_it = volumes_.find(probe.string());
  if (bucket_it == volumes_.end()) return;
  auto& bucket = bucket_it->second;
  bucket.bytes = (bucket.bytes > size) ? bucket.bytes - size : 0;
}

}  // namespace cil
