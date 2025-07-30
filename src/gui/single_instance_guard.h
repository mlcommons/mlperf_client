#ifndef SINGLE_INSTANCE_GUARD_H_
#define SINGLE_INSTANCE_GUARD_H_

#include <QDebug>
#include <QDir>
#include <QLockFile>

#ifdef WIN32
#include <Windows.h>
#endif

namespace gui {

class SingleInstanceGuard {
 public:
  explicit SingleInstanceGuard(
      const QString& lock_file_path = "MLPerf_Client.lock",
      const QString& main_window_title = "MLPerf Client")
      : lock_file_(QDir::temp().absoluteFilePath(lock_file_path)),
        main_window_title_(main_window_title) {}

  // Tries to acquire the lock. Returns true if successful, false if another
  // instance is running.
  bool TryLock() { return lock_file_.tryLock(); }

  void BringInstanceWindowToFront() {
#ifdef WIN32
    qint64 pid = 0;
    QString host;
    QString app_name;
    if (lock_file_.getLockInfo(&pid, &host, &app_name)) {
      if (BringWindowToFrontByPidAndTitle(static_cast<DWORD>(pid),
                                          main_window_title_.toStdWString())) {
        qDebug() << "Brought window of PID" << pid << "to front.";
      } else {
        qDebug() << "Could not bring window to front.";
      }
    }
#endif
  }

 private:
#ifdef WIN32
  bool BringWindowToFrontByPidAndTitle(DWORD pid,
                                       const std::wstring& window_title) {
    struct IOParam {
      DWORD pid_in;
      std::wstring window_title_in;
      HWND result_out;
    };

    IOParam io_param = {pid, window_title, nullptr};

    auto enum_windows_callback = [](HWND handle, LPARAM l_param) {
      DWORD window_pid;
      GetWindowThreadProcessId(handle, &window_pid);
      auto* io_param = reinterpret_cast<IOParam*>(l_param);

      if (window_pid == io_param->pid_in && IsWindowVisible(handle)) {
        wchar_t window_title[512];
        GetWindowTextW(handle, window_title,
                       sizeof(window_title) / sizeof(wchar_t));
        if (wcsstr(window_title, io_param->window_title_in.c_str()) !=
            nullptr) {
          io_param->result_out = handle;
          return FALSE;  // Stop enumeration
        }
      }
      return TRUE;  // Continue enumeration
    };

    EnumWindows(enum_windows_callback, reinterpret_cast<LPARAM>(&io_param));

    HWND hwnd = io_param.result_out;
    if (!hwnd) return false;

    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    return true;
  }
#endif  // WIN32

  QLockFile lock_file_;
  QString main_window_title_;
};

}  // namespace gui

#endif  // SINGLE_INSTANCE_GUARD_H_
