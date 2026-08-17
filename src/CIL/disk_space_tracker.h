#ifndef DISK_SPACE_TRACKER_H_
#define DISK_SPACE_TRACKER_H_

#include <log4cxx/logger.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace cil {

namespace fs = std::filesystem;

class DiskSpaceTracker {
 public:
  void RegisterPlannedDownload(const fs::path& dest_path, uint64_t size);

  // Returns true if every registered destination volume has enough free space
  // (payload + ~50% of the largest file as merge headroom). On failure logs the
  // shortfall via @p logger and, when @p failure_message is non-null, also
  // writes a short user-facing summary of every short volume into it.
  bool HasEnoughSpace(const log4cxx::LoggerPtr& logger,
                      const std::string& context,
                      std::string* failure_message = nullptr) const;

  void MarkCompleted(const fs::path& dest_path);

 private:
  struct Bucket {
    fs::path path;
    uint64_t bytes = 0;
    uint64_t largest = 0;
  };

  static fs::path VolumeOf(const fs::path& dest_path);

  std::unordered_map<std::string, Bucket> volumes_;
  std::unordered_map<std::string, uint64_t> files_;
};

}  // namespace cil

#endif  // DISK_SPACE_TRACKER_H_
