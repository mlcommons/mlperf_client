/**
 * @file ios_utils.h
 * @brief iOS-specific utility functions for the GUI module.
 *
 * Provides helpers for accessing iOS directories and managing device power/sleep.
 */

#ifndef GUI_IOS_UTILS_H_
#define GUI_IOS_UTILS_H_

#include <string>

namespace gui {
namespace ios_utils {

/**
 * @brief Get path to the iOS Documents directory.
 */
std::string GetDocumentsDirectoryPath();

/**
 * @brief Enable or disable the device idle timer.
 * @param disabled Boolean indicating whether to disable the idle timer (true) or enable it (false).
 */
void SetIdleTimerDisabled(bool disabled);

}  // namespace ios_utils
}  // namespace gui

#endif  // GUI_IOS_UTILS_H_
