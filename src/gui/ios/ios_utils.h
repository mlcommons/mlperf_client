/**
 * @file ios_utils.h
 * @brief iOS-specific utility functions for the GUI module.
 *
 * Provides helpers for accessing iOS directories and managing device power/sleep.
 */

#ifndef GUI_IOS_UTILS_H_
#define GUI_IOS_UTILS_H_

#include <QUrl>

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

/**
 * @brief Opens the native iOS share sheet to share a file.
 * @param url The URL of the file to be shared. Must point to a valid,
 * accessible file.
 * @return True if the share sheet was successfully presented
 */
bool ShareFile(const QUrl& url);

}  // namespace ios_utils
}  // namespace gui

#endif  // GUI_IOS_UTILS_H_
