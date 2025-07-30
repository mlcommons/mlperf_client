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
QString BytesToGbString(size_t bytes);

QString EPCardName(const QString& file_name, const QString& ep_name);

}  // namespace utils
}  // namespace gui

#endif  // GUI_UTILS_H_
