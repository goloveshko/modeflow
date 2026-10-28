#pragma once

#include <QList>
#include <QMap>
#include <QRect>
#include <QString>
#include <functional>

namespace ModeFlow::Services {

/**
 * @brief What makes a window the same window across a display switch.
 *
 * A handle alone is not enough: Windows reuses handle values, and a restarted application gets new
 * ones. The process instance (PID plus its start time) plus the window class narrows the match down;
 * two windows of one class in one process are told apart only when the saved handle still names one
 * of them, because guessing between them would move the wrong window.
 */
struct WindowIdentity {
    qint64 pid = 0;
    qint64 processStart = 0;
    QString windowClass;

    bool operator==(const WindowIdentity&) const = default;
};

struct WindowPlacement {
    WindowIdentity identity;
    // The handle seen at capture time, used only as a hint while the window still exists.
    quintptr handle = 0;
    // The rectangle the window occupies when it is not maximized.
    QRect geometry;
    bool maximized = false;
    bool minimized = false;
};

struct LiveWindow {
    WindowIdentity identity;
    quintptr handle = 0;
    QRect geometry;
    bool maximized = false;
    bool minimized = false;
};

/**
 * @brief Everything the layout feature needs from the operating system.
 *
 * Tests replace it, so no test enumerates or moves the windows the user has open.
 */
class IWindowBackend {
public:
    virtual ~IWindowBackend() = default;

    // Visible top-level windows of other processes that are neither cloaked nor owned by a dialog.
    virtual QList<LiveWindow> liveWindows() const = 0;

    // Puts one window back with its saved state; the implementation keeps it inside the work area of
    // its screen and leaves the z-order and the focus alone.
    virtual void placeWindow(quintptr handle, const QRect& geometry, bool maximized, bool minimized) = 0;
};

/**
 * @brief Real backend: the visible top-level windows of the session, moved with SetWindowPos.
 */
class SystemWindowBackend final : public IWindowBackend {
public:
    QList<LiveWindow> liveWindows() const override;
    void placeWindow(quintptr handle, const QRect& geometry, bool maximized, bool minimized) override;
};

/**
 * @brief Layout of the user's windows, stored per screen configuration.
 *
 * The keys come from the screen topology, not from the profile: coming back to the same screens
 * restores the layout that belonged to them, no matter which profile led there.
 */
class WindowLayouts {
public:
    using TopologyProvider = std::function<QString()>;

    /**
     * @param backend Access to the windows of the session.
     * @param storagePath Where remembered layouts survive a restart; empty keeps them in memory only.
     * @param topology What the layouts are keyed by; defaults to the screens of the session.
     */
    explicit WindowLayouts(IWindowBackend* backend, QString storagePath = {}, TopologyProvider topology = {});

    /// Signature of the screens: a different set, resolution or scale means a different layout.
    QString topologyKey() const;

    QList<WindowPlacement> capture() const;

    /**
     * @brief Puts the windows of a saved layout back where they were.
     *
     * Only windows that can be identified are moved. A window that appeared later, an application
     * that was restarted, and a pair of windows that cannot be told apart keep their current place.
     * @return The number of windows that were placed.
     */
    int restore(const QList<WindowPlacement>& layout) const;

    void remember(const QString& topology, const QList<WindowPlacement>& layout);
    QList<WindowPlacement> layoutFor(const QString& topology) const;
    void forget(const QString& topology);

    bool load(const QString& path);
    bool save(const QString& path) const;

private:
    void persist() const;

private:
    IWindowBackend* m_backend;
    QString m_storagePath;
    TopologyProvider m_topology;
    QMap<QString, QList<WindowPlacement>> m_layouts;
};

} // namespace ModeFlow::Services
