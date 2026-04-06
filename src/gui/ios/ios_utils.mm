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

static UIWindowScene* GetActiveWindowScene() {
  for (UIScene* s in [UIApplication sharedApplication].connectedScenes) {
    if (s.activationState == UISceneActivationStateForegroundActive &&
        [s isKindOfClass:[UIWindowScene class]]) {
      return (UIWindowScene*)s;
    }
  }
  return nil;
}

static UIWindow* GetKeyWindow(UIWindowScene* scene) {
  if (!scene) return nil;

  for (UIWindow* w in scene.windows)
    if (w.isKeyWindow) return w;

  // Fallback
  for (UIWindow* w in scene.windows)
    if (!w.hidden && w.alpha > 0.0 && w.windowLevel == UIWindowLevelNormal) return w;

  return scene.windows.firstObject;
}

bool ShareFile(const QUrl& url) {
  NSURL* file_url = url.toNSURL();
  NSItemProvider* provider =
      [[NSItemProvider alloc] initWithContentsOfURL:file_url];
  provider.suggestedName = file_url.lastPathComponent;
  NSArray* items = @[ provider ];

  UIWindowScene* scene = GetActiveWindowScene();
  if (!scene) {
    NSLog(@"[ShareFile] No active UIWindowScene found");
    return false;
  }

  UIWindow* window = GetKeyWindow(scene);
  if (!window) {
    NSLog(@"[ShareFile] No UIWindow found in active scene");
    return false;
  }

  UIViewController* root_vc = window.rootViewController;
  while (root_vc.presentedViewController)
    root_vc = root_vc.presentedViewController;

  UIActivityViewController* activity_vc =
      [[UIActivityViewController alloc] initWithActivityItems:items
                                        applicationActivities:nil];

  if (activity_vc.popoverPresentationController) {
    activity_vc.popoverPresentationController.sourceView = root_vc.view;
    activity_vc.popoverPresentationController.sourceRect =
        CGRectMake(CGRectGetMidX(root_vc.view.bounds),
                   CGRectGetMidY(root_vc.view.bounds), 1, 1);
    activity_vc.popoverPresentationController.permittedArrowDirections = 0;
  }

  [root_vc presentViewController:activity_vc animated:YES completion:nil];
  return true;
}


}  // namespace ios_utils
}  // namespace gui
