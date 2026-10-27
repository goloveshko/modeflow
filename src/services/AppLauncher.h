#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QTimer>

#include "ConfigTypes.h"

namespace ModeFlow::Services {

class AppLauncher : public QObject {
    Q_OBJECT
public:
    explicit AppLauncher(QObject* parent = nullptr);
    ~AppLauncher() override;

    /**
     * @brief Start a profile's applications.
     *
     * @return false as soon as one immediate launch was refused. A delayed launch only counts as
     * scheduled here: it is still in the future, so its own error arrives when the timer fires.
     */
    virtual bool launchSequence(const QString& profileId, const QList<Core::AppLaunchConfig>& apps);

    /**
     * @brief Stop launches that are still scheduled for a profile.
     *
     * Safe to call at any point of a switch: a delayed application must not appear on top of a
     * workspace the user has already left. Running processes are not touched.
     */
    virtual void cancelDelayedLaunches(const QString& profileId);

    /**
     * @brief Ask the applications a profile launched to close.
     *
     * The request goes to their own windows, so each application can save data or refuse. Nothing is
     * ever killed, and a process that already exited or has no window is only forgotten.
     */
    virtual void closeProfileProcesses(const QString& profileId);

signals:
    void errorOccurred(const QString& message);

private:
    struct TrackedProcess {
        qint64 pid = 0;
        // Kept open while we track the process: Windows does not recycle a PID that still has a
        // handle, so this is what proves the close request is aimed at the process we launched.
        void* handle = nullptr;
    };

    /**
     * @brief The one executable boundary every launch path must pass.
     *
     * Profiles can arrive from an imported file, so the check cannot live in the UI-facing entry point
     * only. On failure the user-facing reason is written to @p error.
     */
    bool validateExecutable(const QString& path, QString& error) const;

    qint64 execute(const QString& path);
    bool launchAndTrack(const QString& profileId, const Core::AppLaunchConfig& app);

    QMap<QString, QList<TrackedProcess>> m_activeProcesses;

    // Tracks active delayed launch timers for each profile ID
    QMap<QString, QList<QTimer*>> m_pendingTimers;
};
} // namespace ModeFlow::Services