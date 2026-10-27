#pragma once

#include <QWidget>

namespace ModeFlow::Utils {

class SystemUtils {
public:
    // Returns true if the Windows Graphics driver and Qt Screen system are initialized
    static bool isSystemReady();

    // Returns true if the interactive user desktop is active (not the logon screen)
    static bool isDesktopActive();

    // Optional: a helper to get the executable's directory without relying on QCoreApplication
    static QString getExecutableDir();

    static bool isWin11();
    static bool isWin11_22H2();
    static bool isSystemDarkMode();
    static void configureWindowButtons(QWidget* widget, bool allowMinimize, bool allowMaximize);

    static bool isValidExecutablePath(const QString& path);

    /**
     * @brief Open an opaque handle to a process, or null when it is already gone.
     *
     * Keeping the handle open is what makes the PID trustworthy: Windows does not recycle a process
     * identifier while a handle to it is still held.
     */
    static void* openProcessHandle(qint64 pid);

    static bool isProcessRunning(void* handle);
    static void closeProcessHandle(void* handle);

    /**
     * @brief Ask a process's own top-level windows to close, the same way the close button does.
     *
     * The application may save data or refuse, so nothing is killed. Returns false when the process
     * has no window to ask.
     */
    static bool requestProcessClose(qint64 pid);

    static bool restartApplication(const QStringList& arguments);
};
} // namespace ModeFlow::Utils
