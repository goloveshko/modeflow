#include "AppLauncher.h"

#include <QDir>
#include <QProcess>

#include "Logging.h"
#include "SystemUtils.h"

namespace ModeFlow::Services {
using namespace Qt::StringLiterals;

namespace {
QStringList getBlockedSystemPaths() {
    const QString winDir = qEnvironmentVariable("windir");
    if (winDir.isEmpty()) {
        qCWarning(lcService) << "Failed to retrieve %windir% environment variable.";
        return {};
    }

    const QDir winQDir(winDir);

    return {QDir::toNativeSeparators(winQDir.filePath(u"System32"_s)),
            QDir::toNativeSeparators(winQDir.filePath(u"SysWOW64"_s)),
            QDir::toNativeSeparators(winQDir.filePath(u"Sysnative"_s)),
            QDir::toNativeSeparators(winQDir.filePath(u"Microsoft.NET"_s))};
}
} // namespace

AppLauncher::AppLauncher(QObject* parent) : QObject(parent) {}

AppLauncher::~AppLauncher() {
    for (const auto& processes : std::as_const(m_activeProcesses)) {
        for (const auto& process : processes) {
            Utils::SystemUtils::closeProcessHandle(process.handle);
        }
    }
}

bool AppLauncher::validateExecutable(const QString& path, QString& error) const {
    QFileInfo checkFile(path);

    if (checkFile.isSymLink()) {
        qCWarning(lcService) << "Symlinks are not allowed:" << path;
        error = tr("Refusing to launch a symbolic link: %1").arg(path);
        return false;
    }

    if (!checkFile.exists() || !checkFile.isFile()) {
        qCWarning(lcService) << "File does not exist:" << path;
        error = tr("Application file does not exist: %1").arg(path);
        return false;
    }

    const QString canonicalPath = QDir::toNativeSeparators(checkFile.canonicalFilePath());
    const QStringList blockedPaths = getBlockedSystemPaths();

    for (const auto& blockedPath : blockedPaths) {
        QDir blockedDir(blockedPath);
        const QString relative = blockedDir.relativeFilePath(canonicalPath);

        if (!relative.startsWith(u"../"_s) && relative != u".."_s) {
            qCWarning(lcService) << "System path blocked (containment detected):" << canonicalPath;
            error = tr("Launching system executables is blocked: %1").arg(canonicalPath);
            return false;
        }
    }

    const QString suffix = checkFile.suffix().toLower();
    if (suffix != u"exe"_s) {
        qCWarning(lcService) << "Only .exe files are allowed:" << path << " (." << suffix << ")";
        error = tr("Only .exe files can be launched: %1").arg(path);
        return false;
    }

    return true;
}

bool AppLauncher::launchSequence(const QString& profileId, const QList<Core::AppLaunchConfig>& apps) {
    bool immediateLaunchesAccepted = true;
    int scheduledCount = 0;

    for (const auto& app : apps) {
        if (app.appPath.isEmpty())
            continue;

        // If there's a startup delay, we instantiate a trackable QTimer instead of an anonymous singleShot.
        // This allows us to abort the delayed launch if the user switches away from this profile before the delay
        // expires.
        if (app.delaySeconds > 0) {
            auto* timer = new QTimer(this);
            timer->setSingleShot(true);
            timer->setInterval(std::chrono::seconds(app.delaySeconds));

            connect(timer, &QTimer::timeout, this, [this, timer, profileId, app]() {
                auto& list = m_pendingTimers[profileId];
                list.removeOne(timer);
                if (list.isEmpty()) {
                    m_pendingTimers.remove(profileId);
                }
                timer->deleteLater();

                launchAndTrack(profileId, app);
            });

            m_pendingTimers[profileId].append(timer);
            timer->start();
            ++scheduledCount;
        } else if (!launchAndTrack(profileId, app)) {
            immediateLaunchesAccepted = false;
        }
    }

    if (scheduledCount > 0) {
        qCDebug(lcService) << "Scheduled" << scheduledCount << "delayed launches for profile" << profileId;
    }

    return immediateLaunchesAccepted;
}

void AppLauncher::cancelDelayedLaunches(const QString& profileId) {
    auto timerIt = m_pendingTimers.find(profileId);
    if (timerIt == m_pendingTimers.end()) {
        return;
    }

    qCDebug(lcService) << "Cancelling" << timerIt.value().size() << "pending delayed launches for profile" << profileId;

    for (QTimer* timer : timerIt.value()) {
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }
    }
    m_pendingTimers.erase(timerIt);
}

void AppLauncher::closeProfileProcesses(const QString& profileId) {
    auto it = m_activeProcesses.find(profileId);
    if (it == m_activeProcesses.end()) {
        return;
    }

    for (const TrackedProcess& process : *it) {
        // An exited process can no longer be closed, and without a live handle the PID is not ours
        // to act on any more.
        if (!Utils::SystemUtils::isProcessRunning(process.handle)) {
            Utils::SystemUtils::closeProcessHandle(process.handle);
            continue;
        }

        if (Utils::SystemUtils::requestProcessClose(process.pid)) {
            qCDebug(lcService) << "Asked process PID" << process.pid << "to close for profile" << profileId;
        } else {
            qCWarning(lcService) << "Process PID" << process.pid << "has no window to ask, leaving it running";
        }

        Utils::SystemUtils::closeProcessHandle(process.handle);
    }

    m_activeProcesses.erase(it);
}

qint64 AppLauncher::execute(const QString& path) {
    qCDebug(lcService) << "Launching actual process:" << path;

    const QString workingDir = QFileInfo(path).absolutePath();
    qint64 pid = 0;
    const bool success = QProcess::startDetached(path, QStringList(), workingDir, &pid);

    if (!success) {
        qCWarning(lcService) << "Failed to start process:" << path;
        emit errorOccurred(tr("Failed to start application: %1").arg(path));
        return 0;
    }

    return pid;
}

bool AppLauncher::launchAndTrack(const QString& profileId, const Core::AppLaunchConfig& app) {
    QString error;
    if (!validateExecutable(app.appPath, error)) {
        // A delayed launch fires long after the profile finished applying, so its message has to say
        // where it came from instead of reading like a launch the user just asked for.
        emit errorOccurred(app.delaySeconds > 0 ? tr("The scheduled application could not be launched: %1").arg(error)
                                                : error);
        return false;
    }

    const qint64 pid = execute(app.appPath);
    if (pid <= 0) {
        return false;
    }

    if (!app.closeOnExit) {
        return true;
    }

    void* handle = Utils::SystemUtils::openProcessHandle(pid);
    if (!handle) {
        // Without a handle there is no way to tell later whether this PID still names our process,
        // and closing a stranger's windows is not worth the risk. The application is running, we just
        // cannot follow it.
        qCWarning(lcService) << "Cannot watch process PID" << pid << ", it will be left running";
        return true;
    }

    m_activeProcesses[profileId].append(TrackedProcess{pid, handle});
    qCDebug(lcService) << "Tracked process PID" << pid << "for profile" << profileId << "(closeOnExit is active)";
    return true;
}

} // namespace ModeFlow::Services
