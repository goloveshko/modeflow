#include "WindowLayouts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScreen>
#include <iterator>
#include <utility>

#include "Logging.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#endif

namespace ModeFlow::Services {

using namespace Qt::StringLiterals;

namespace {

QString screenSignature(const QScreen* screen) {
    // The model travels with the device name: Windows may hand out the same GDI name to a different
    // monitor after the cables are swapped, and its layout should not follow the name.
    QString model = screen->model();
    if (model.isEmpty()) {
        model = screen->manufacturer();
    }

    return QStringLiteral("%1|%2|%3x%4@%5")
        .arg(screen->name(), model)
        .arg(screen->geometry().width())
        .arg(screen->geometry().height())
        .arg(QString::number(screen->devicePixelRatio(), 'f', 2));
}

QString systemTopologyKey() {
    QStringList signatures;
    const auto screens = QGuiApplication::screens();
    signatures.reserve(screens.size());

    for (const QScreen* screen : screens) {
        signatures.append(screenSignature(screen));
    }

    // The same screens reported in another order are the same configuration.
    signatures.sort();
    return signatures.join(u';');
}

QJsonObject placementToJson(const WindowPlacement& placement) {
    QJsonObject object;
    object[u"pid"_s] = placement.identity.pid;
    object[u"startedAt"_s] = placement.identity.processStart;
    object[u"windowClass"_s] = placement.identity.windowClass;
    object[u"x"_s] = placement.geometry.x();
    object[u"y"_s] = placement.geometry.y();
    object[u"width"_s] = placement.geometry.width();
    object[u"height"_s] = placement.geometry.height();
    object[u"maximized"_s] = placement.maximized;
    object[u"minimized"_s] = placement.minimized;
    return object;
}

WindowPlacement placementFromJson(const QJsonObject& object) {
    WindowPlacement placement;
    placement.identity.pid = object[u"pid"_s].toInteger();
    placement.identity.processStart = object[u"startedAt"_s].toInteger();
    placement.identity.windowClass = object[u"windowClass"_s].toString();
    placement.geometry =
        QRect(object[u"x"_s].toInt(), object[u"y"_s].toInt(), object[u"width"_s].toInt(), object[u"height"_s].toInt());
    placement.maximized = object[u"maximized"_s].toBool();
    placement.minimized = object[u"minimized"_s].toBool();
    return placement;
}

#ifdef Q_OS_WIN

QRect rectOf(const RECT& rect) {
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

QString windowClassOf(HWND hwnd) {
    wchar_t buffer[256];
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    return QString::fromWCharArray(buffer, qMax(length, 0));
}

// Zero would be indistinguishable from an unreadable start time, so the caller gets -1 instead.
qint64 processStartTicks(DWORD pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return -1;
    }
    const auto closeProcess = qScopeGuard([process]() { CloseHandle(process); });

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
        return -1;
    }

    ULARGE_INTEGER ticks;
    ticks.LowPart = creation.dwLowDateTime;
    ticks.HighPart = creation.dwHighDateTime;
    return static_cast<qint64>(ticks.QuadPart);
}

bool isCloaked(HWND hwnd) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

// Keeps the restored window reachable: a layout saved on a monitor that is gone would otherwise put
// it outside the desktop, where the user cannot grab it.
QRect clampToWorkArea(const QRect& rect) {
    const RECT bounds{rect.left(), rect.top(), rect.right() + 1, rect.bottom() + 1};
    const HMONITOR monitor = MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
        return rect;
    }

    const QRect work = rectOf(info.rcWork);
    QRect target = rect;
    target.setWidth(qMin(target.width(), work.width()));
    target.setHeight(qMin(target.height(), work.height()));
    target.moveLeft(qBound(work.left(), target.left(), work.right() - target.width() + 1));
    target.moveTop(qBound(work.top(), target.top(), work.bottom() - target.height() + 1));
    return target;
}

struct CollectContext {
    QList<LiveWindow>* windows;
};

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM data) {
    auto* context = reinterpret_cast<CollectContext*>(data);

    // A minimized window is kept: its restored rectangle still belongs to the layout, and the next
    // screen may be the only one that shows it again.
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 || isCloaked(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return TRUE;
    }

    const qint64 startedAt = processStartTicks(pid);
    if (startedAt < 0) {
        // Without the start time the window cannot be told apart from a later process that reuses
        // the same PID, and moving that one would be worse than leaving this one alone.
        return TRUE;
    }

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(hwnd, &placement)) {
        return TRUE;
    }

    // The restored rectangle, not the maximized one: the placement API restores into this rectangle,
    // and a maximized window's own rect covers the taskbar.
    const QRect geometry = rectOf(placement.rcNormalPosition);
    if (!geometry.isValid()) {
        return TRUE;
    }

    LiveWindow window;
    window.identity.pid = pid;
    window.identity.processStart = startedAt;
    window.identity.windowClass = windowClassOf(hwnd);
    window.handle = reinterpret_cast<quintptr>(hwnd);
    window.geometry = geometry;
    window.minimized = placement.showCmd == SW_SHOWMINIMIZED;
    window.maximized =
        placement.showCmd == SW_SHOWMAXIMIZED || (window.minimized && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
    context->windows->append(window);
    return TRUE;
}

#endif

} // namespace

WindowLayouts::WindowLayouts(IWindowBackend* backend, QString storagePath, TopologyProvider topology)
    : m_backend(backend), m_storagePath(std::move(storagePath)), m_topology(std::move(topology)) {
    if (!m_storagePath.isEmpty()) {
        load(m_storagePath);
    }
}

QString WindowLayouts::topologyKey() const {
    return m_topology ? m_topology() : systemTopologyKey();
}

QList<WindowPlacement> WindowLayouts::capture() const {
    QList<WindowPlacement> placements;

    for (const LiveWindow& window : m_backend->liveWindows()) {
        WindowPlacement placement;
        placement.identity = window.identity;
        placement.handle = window.handle;
        placement.geometry = window.geometry;
        placement.maximized = window.maximized;
        placement.minimized = window.minimized;
        placements.append(placement);
    }

    return placements;
}

int WindowLayouts::restore(const QList<WindowPlacement>& layout) const {
    const QList<LiveWindow> live = m_backend->liveWindows();
    int placed = 0;

    for (const WindowPlacement& placement : layout) {
        const LiveWindow* match = nullptr;
        int candidates = 0;

        for (const LiveWindow& window : live) {
            if (!(window.identity == placement.identity)) {
                continue;
            }
            ++candidates;
            if (match == nullptr || window.handle == placement.handle) {
                match = &window;
            }
        }

        if (match == nullptr) {
            continue;
        }

        // Two windows of one class in one process share an identity. The handle saved with the
        // layout tells them apart; without it, choosing either one would move the wrong window.
        if (candidates > 1 && match->handle != placement.handle) {
            continue;
        }

        m_backend->placeWindow(match->handle, placement.geometry, placement.maximized, placement.minimized);
        ++placed;
    }

    return placed;
}

void WindowLayouts::remember(const QString& topology, const QList<WindowPlacement>& layout) {
    if (topology.isEmpty()) {
        return;
    }

    m_layouts[topology] = layout;
    persist();
}

void WindowLayouts::persist() const {
    if (m_storagePath.isEmpty()) {
        return;
    }

    QDir().mkpath(QFileInfo(m_storagePath).absolutePath());
    save(m_storagePath);
}

QList<WindowPlacement> WindowLayouts::layoutFor(const QString& topology) const {
    return m_layouts.value(topology);
}

void WindowLayouts::forget(const QString& topology) {
    m_layouts.remove(topology);
}

bool WindowLayouts::load(const QString& path) {
    QFile file(path);
    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcService) << "Cannot read window layouts from" << path;
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        qCWarning(lcService) << "Window layouts file is not an object:" << path;
        return false;
    }

    QMap<QString, QList<WindowPlacement>> loaded;
    const QJsonObject root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QList<WindowPlacement> placements;
        for (const QJsonValue& value : it.value().toArray()) {
            if (value.isObject()) {
                placements.append(placementFromJson(value.toObject()));
            }
        }
        loaded.insert(it.key(), placements);
    }

    m_layouts = std::move(loaded);
    return true;
}

bool WindowLayouts::save(const QString& path) const {
    QJsonObject root;
    for (auto it = m_layouts.begin(); it != m_layouts.end(); ++it) {
        QJsonArray placements;
        for (const WindowPlacement& placement : it.value()) {
            placements.append(placementToJson(placement));
        }
        root.insert(it.key(), placements);
    }

    // A half-written file would be read as a layout with fewer windows, so the replacement is atomic
    // for the same reason profile export is.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcService) << "Cannot write window layouts to" << path;
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

#ifdef Q_OS_WIN

QList<LiveWindow> SystemWindowBackend::liveWindows() const {
    QList<LiveWindow> windows;
    CollectContext context{&windows};
    EnumWindows(&collectWindow, reinterpret_cast<LPARAM>(&context));
    return windows;
}

void SystemWindowBackend::placeWindow(quintptr handle, const QRect& geometry, bool maximized, bool minimized) {
    const HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!IsWindow(hwnd)) {
        return;
    }

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(hwnd, &placement)) {
        return;
    }

    const QRect target = clampToWorkArea(geometry);
    placement.rcNormalPosition = {target.left(), target.top(), target.right() + 1, target.bottom() + 1};
    if (minimized) {
        placement.showCmd = SW_SHOWMINIMIZED;
        placement.flags = maximized ? WPF_RESTORETOMAXIMIZED : 0;
    } else {
        placement.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        placement.flags = 0;
    }

    // SetWindowPlacement resizes, maximizes and minimizes without bringing the window to the
    // foreground, so a restore does not steal focus from whatever the user is doing.
    SetWindowPlacement(hwnd, &placement);
}

#else

QList<LiveWindow> SystemWindowBackend::liveWindows() const {
    return {};
}

void SystemWindowBackend::placeWindow(quintptr, const QRect&, bool, bool) {}

#endif

} // namespace ModeFlow::Services
