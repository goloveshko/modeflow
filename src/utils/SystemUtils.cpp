#include "SystemUtils.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <vector>

#include "Logging.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ModeFlow::Utils {

using namespace Qt::StringLiterals;

#ifdef Q_OS_WIN
namespace {
struct CloseVisitor {
    DWORD targetPid = 0;
    bool askedAnyWindow = false;
};

BOOL CALLBACK askWindowToClose(HWND hwnd, LPARAM lParam) {
    auto* visitor = reinterpret_cast<CloseVisitor*>(lParam);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);

    // Only a visible top-level window without an owner: that is the window the user would close, and
    // hidden helper or owned windows would not let the application react to the request.
    if (windowPid == visitor->targetPid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        visitor->askedAnyWindow = true;
    }

    return TRUE;
}
} // namespace
#endif // Q_OS_WIN

bool SystemUtils::isSystemReady() {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return false;
    }

    if (QGuiApplication::screens().isEmpty()) {
        return false;
    }

    if (!QGuiApplication::primaryScreen() || QGuiApplication::primaryScreen()->name().isEmpty()) {
        return false;
    }

    if (auto* primary = QGuiApplication::primaryScreen()) {
        qCDebug(lcUtil) << "Primary screen:" << primary->name() << ", model:" << primary->model()
                        << ", manufacturer:" << primary->manufacturer() << ", serialNumber:" << primary->serialNumber()
                        << ", size:" << primary->size();
    }

    return true;
}

bool SystemUtils::isDesktopActive() {
    // Attempt to open the input desktop.
    // If this fails, the Logon screen or UAC prompt is likely blocking hardware access.
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (hDesk) {
        CloseDesktop(hDesk);
        return true;
    }
    return false;
}

QString SystemUtils::getExecutableDir() {
    DWORD bufferSize = MAX_PATH;
    std::vector<wchar_t> buffer(bufferSize);

    while (true) {
        const DWORD copied = GetModuleFileNameW(NULL, buffer.data(), bufferSize);
        if (copied == 0)
            return QString();

        if (copied < bufferSize) {
            return QFileInfo(QString::fromWCharArray(buffer.data(), copied)).absolutePath();
        }

        bufferSize *= 2;
        buffer.resize(bufferSize);
    }
}

bool SystemUtils::isWin11() {
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
}

bool SystemUtils::isWin11_22H2() {
    return QOperatingSystemVersion::current() >=
           QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621);
}

bool SystemUtils::isSystemDarkMode() {
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows10) {
        return false;
    }
    // Theme settings in Windows 10/11 are stored here
    QSettings settings(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                       QSettings::NativeFormat);

    // SystemUsesLightTheme = 0 means Dark Mode for system (taskbar)
    return !settings.value(u"AppsUseLightTheme"_s, true).toBool();
}

void SystemUtils::configureWindowButtons(QWidget* widget, bool allowMinimize, bool allowMaximize) {
    if (!widget)
        return;

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);

    if (allowMinimize) {
        style |= static_cast<LONG_PTR>(WS_MINIMIZEBOX);
    } else {
        style &= ~static_cast<LONG_PTR>(WS_MINIMIZEBOX);
    }

    if (allowMaximize) {
        style |= static_cast<LONG_PTR>(WS_MAXIMIZEBOX);
    } else {
        style &= ~static_cast<LONG_PTR>(WS_MAXIMIZEBOX);
    }

    ::SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

bool SystemUtils::isValidExecutablePath(const QString& path) {
    if (path.isEmpty())
        return false;
    QFileInfo fi(path);
    return fi.exists() && fi.isFile() && !fi.isSymLink() && fi.suffix().toLower() == "exe";
}

void* SystemUtils::openProcessHandle(qint64 pid) {
    if (pid <= 0)
        return nullptr;

#ifdef Q_OS_WIN
    // Watching a process needs only synchronize and query rights, which also works for a process
    // that is not a child of this one.
    return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
#else
    return nullptr;
#endif
}

bool SystemUtils::isProcessRunning(void* handle) {
#ifdef Q_OS_WIN
    if (!handle)
        return false;

    // A zero-timeout wait avoids the STILL_ACTIVE ambiguity, where an exit code of 259 would look
    // like a living process.
    return WaitForSingleObject(static_cast<HANDLE>(handle), 0) == WAIT_TIMEOUT;
#else
    Q_UNUSED(handle);
    return false;
#endif
}

void SystemUtils::closeProcessHandle(void* handle) {
#ifdef Q_OS_WIN
    if (handle) {
        CloseHandle(static_cast<HANDLE>(handle));
    }
#else
    Q_UNUSED(handle);
#endif
}

bool SystemUtils::requestProcessClose(qint64 pid) {
#ifdef Q_OS_WIN
    CloseVisitor visitor{static_cast<DWORD>(pid)};
    EnumWindows(askWindowToClose, reinterpret_cast<LPARAM>(&visitor));
    return visitor.askedAnyWindow;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

bool SystemUtils::restartApplication(const QStringList& arguments) {
    qCDebug(lcUtil) << "Spawning detached application instance with args:" << arguments;
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

} // namespace ModeFlow::Utils
