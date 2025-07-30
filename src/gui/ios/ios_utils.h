#ifndef GUI_IOS_UTILS_H_
#define GUI_IOS_UTILS_H_

#include <string>

namespace gui {
namespace ios_utils {

std::string GetDocumentsDirectoryPath();

void SetIdleTimerDisabled(bool disabled);

}  // namespace ios_utils
}  // namespace gui

#endif  // GUI_IOS_UTILS_H_
