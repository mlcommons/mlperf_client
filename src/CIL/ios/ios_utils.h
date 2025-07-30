#ifndef IOS_UTILS_H_
#define IOS_UTILS_H_

#include <string>

namespace cil {
namespace ios_utils {

std::string GetDocumentsDirectoryPath();

std::string GetIOSLibraryPath(const std::string &lib_name);

std::string GetBundleResourcePath(const std::string &resource_name,
                                  const std::string &extension);

}  // namespace ios_utils
}  // namespace cil

#endif  // IOS_UTILS_H_
