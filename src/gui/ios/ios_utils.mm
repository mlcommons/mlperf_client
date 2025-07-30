#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "ios_utils.h"

namespace gui {
namespace ios_utils {

std::string GetDocumentsDirectoryPath() {
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                       NSUserDomainMask, YES);
  NSString *documents_directory = [paths firstObject];

  return std::string([documents_directory UTF8String]);
}

void SetIdleTimerDisabled(bool disabled) {
  dispatch_async(dispatch_get_main_queue(), ^{
    [UIApplication sharedApplication].idleTimerDisabled = disabled;
  });
}

}  // namespace ios_utils
}  // namespace gui
