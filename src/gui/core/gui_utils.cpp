#include "gui_utils.h"

#include <QStringList>

#include "../CIL/benchmark/runner.h"
#include "../CIL/execution_provider.h"
#include "../CIL/utils.h"

namespace gui {
namespace utils {
QString GetDeviceIcon(const QString& device_type) {
  if (device_type == "GPU") return ":/icons/resources/icons/bi_gpu_card.png";
  return ":/icons/resources/icons/ri_npu-line.png";
}

QString GetCPUIcon(const QString& vendor) {
  if (vendor.contains("Intel")) return ":/icons/resources/icons/intel_logo.png";
  if (vendor.contains("AMD") || vendor.contains("Advanced Micro Devices"))
    return ":/icons/resources/icons/amd_logo.png";
  if (vendor.contains("qualcomm", Qt::CaseInsensitive))
    return ":/icons/resources/icons/qualcomm_logo.png";

  return ":/icons/resources/icons/cpu.png";
}

QString GetGPUIcon(const QString& vendor) {
  if (vendor.contains("Intel")) return ":/icons/resources/icons/intel_logo.png";
  if (vendor.contains("nvidia", Qt::CaseInsensitive))
    return ":/icons/resources/icons/nvidia_logo.png";
  if (vendor.contains("AMD") || vendor.contains("Advanced Micro Devices"))
    return ":/icons/resources/icons/amd_logo.png";
  if (vendor.contains("qualcomm", Qt::CaseInsensitive))
    return ":/icons/resources/icons/qualcomm_logo.png";
  return ":/icons/resources/icons/gpu.png";
}

QString GetNPUIcon(const QString& vendor) {
  if (vendor.contains("Intel")) return ":/icons/resources/icons/intel_logo.png";
  if (vendor.contains("AMD") || vendor.contains("Advanced Micro Devices"))
    return ":/icons/resources/icons/amd_logo.png";
  if (vendor.contains("qualcomm", Qt::CaseInsensitive))
    return ":/icons/resources/icons/qualcomm_logo.png";
  return ":/icons/resources/icons/npu.png";
}

QString GetOSIcon(const QString& name) {
  if (name.contains("Windows 10"))
    return ":/icons/resources/icons/windows10.png";
  if (name.contains("Windows 11"))
    return ":/icons/resources/icons/windows11.png";
  if (name.contains("osx", Qt::CaseInsensitive))
    return ":/icons/resources/icons/macos.png";

  return ":/icons/resources/icons/os.png";
}

double BytesToGb(size_t bytes) { return cil::utils::BytesToGb(bytes); }

QString GBToString(double gb) {
  return QString::fromStdString(cil::utils::GBToString(gb));
}

double BytesToNearestGB(size_t bytes) {
  return cil::utils::BytesToNearestGB(bytes);
}

QString BytesToHumanReadableString(uint64_t bytes) {
  double kb = 1024.0;
  double mb = kb * 1024.0;
  double gb = mb * 1024.0;

  if (bytes >= gb) {
    return QString("%1 GB").arg(bytes / gb, 0, 'f', 1);
  } else if (bytes >= mb) {
    return QString("%1 MB").arg(bytes / mb, 0, 'f', 1);
  } else if (bytes >= kb) {
    return QString("%1 KB").arg(bytes / kb, 0, 'f', 0);
  }

  return QString("%1 B").arg(bytes);
}

QString EPCardName(const QString& file_name, const QString& ep_name) {
  QString long_ep_name =
      QString::fromStdString(cil::EPNameToLongName(ep_name.toStdString()));
  QStringList excluded_kit_names = {"DML", "RyzenAI"};
  QStringList name_list = file_name.split('_');
  if (name_list.size() < 3) return long_ep_name;
  int kit_start_index = name_list[1].indexOf('-');
  if (kit_start_index != -1) {
    QString kit_name = name_list[1].mid(kit_start_index + 1);
    if (!excluded_kit_names.contains(kit_name, Qt::CaseInsensitive)) {
      kit_name.replace("-", " ");
      if (!long_ep_name.contains(kit_name, Qt::CaseInsensitive)) {
        long_ep_name += " " + kit_name;
      } else {
        // Check if the first part is different
        QString first_part = name_list[1].left(kit_start_index);
        QString first_part_to_long = QString::fromStdString(
            cil::EPNameToLongName(first_part.toStdString()));
        if (first_part_to_long != long_ep_name &&
            !long_ep_name.contains(first_part, Qt::CaseInsensitive)) {
          long_ep_name = first_part + " " + long_ep_name;
        }
      }
    }
  }
  return name_list[0] + " " + long_ep_name + " " + name_list[2];
}

QString ModelDisplayName(const std::string& model_name) {
  auto model_full_name = cil::BenchmarkRunner::GetModelFullName(
      cil::utils::StringToLowerCase(model_name));

  auto model_display_name =
      model_full_name.has_value() ? model_full_name.value() : model_name;
  return QString::fromStdString(model_display_name);
}

QString NormalizeNewlines(const QString& text) {
  QString result = text;
  for (int i = 1; i < result.size(); ++i)
    if (result[i] == '\n') {
      const QChar prev = result[i - 1];
      if (prev.isPunct()) {
        result[i] = ' ';  // replace '\n' with space
      } else {
        result[i] = ' ';
        result.insert(i, '.');  // insert '.' before the space
        ++i;
      }
    }

  return result;
}

}  // namespace utils
}  // namespace gui
