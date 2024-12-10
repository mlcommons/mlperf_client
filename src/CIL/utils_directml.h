#ifndef UTILS_DIRECTML_H
#define UTILS_DIRECTML_H

#include <string>
#include <vector>

namespace cil {
namespace utils {
namespace DirectML {
#ifdef WIN32

struct AdapterInfo {
  std::string name;
  std::string type;
  unsigned long luid_low_part;
  long luid_high_part;
};

std::vector<AdapterInfo> EnumerateAdapters(const std::string& dlls_directory);

#endif
}  // namespace DirectML
}  // namespace utils

}  // namespace cil

#endif