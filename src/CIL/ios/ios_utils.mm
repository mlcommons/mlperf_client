#import <Foundation/Foundation.h>
#import "ios_utils.h"

namespace cil {
namespace ios_utils {

std::string GetDocumentsDirectoryPath() {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documents_directory = [paths firstObject];
    
    return std::string([documents_directory UTF8String]);
}

std::string GetIOSLibraryPath(const std::string &lib_name) {
    NSString *frameworks_path = [[NSBundle mainBundle] privateFrameworksPath];
    NSString *lib_path = [frameworks_path stringByAppendingPathComponent:[NSString stringWithUTF8String:lib_name.c_str()]];
    
    return std::string([lib_path UTF8String]);
}

std::string GetBundleResourcePath(const std::string &resource_name, const std::string &extension) {
    NSString *path = [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:resource_name.c_str()]
                                                     ofType:[NSString stringWithUTF8String:extension.c_str()]];
    if (!path) return "";
    return std::string([path UTF8String]);
}


}  // namespace ios_utils
}  // namespace cil