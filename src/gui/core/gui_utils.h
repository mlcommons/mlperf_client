/**
 * @file gui_utils.h
 * @brief Utility functions for GUI components and helpers.
 * Provides helpers for common GUI operations, conversions, and event handling.
 */

#ifndef GUI_UTILS_H_
#define GUI_UTILS_H_

#include <QString>

namespace gui {
namespace utils {

QString GetDeviceIcon(const QString& device_type);
QString GetCPUIcon(const QString& vendor);
QString GetGPUIcon(const QString& vendor);
QString GetNPUIcon(const QString& vendor);
QString GetOSIcon(const QString& name);

double BytesToGb(size_t bytes);

QString GBToString(double gb);

double BytesToNearestGB(size_t bytes);

QString BytesToHumanReadableString(uint64_t bytes);

// Generate display name for an execution provider card.
QString EPCardName(const QString& file_name, const QString& ep_name);

QString ModelDisplayName(const std::string& model_name);

QString NormalizeNewlines(const QString& text);

}  // namespace utils
}  // namespace gui

#endif  // GUI_UTILS_H_
