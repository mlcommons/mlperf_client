#ifndef APPLE_UTILS_H_
#define APPLE_UTILS_H_

#include <string>

namespace cil {
namespace apple_utils {

std::string GetDocumentsDirectoryPath();

std::string GetEmbeddedLibraryPath(const std::string &lib_name);

std::string GetBundleResourcePath(const std::string &resource_name,
                                  const std::string &extension);

}  // namespace apple_utils
}  // namespace cil

#endif  // APPLE_UTILS_H_
