#include "DisplayManager.h"

#include <QGuiApplication>
#include <QScopeGuard>

#include "Constants.h"
#include "Logging.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ModeFlow::Services {

using namespace Qt::StringLiterals;

DisplayManager::DisplayManager(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &DisplayManager::onSystemDisplaysChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &DisplayManager::onSystemDisplaysChanged);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &DisplayManager::onSystemDisplaysChanged);

    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(Utils::DisplayDebounceIntervalMs);
    connect(m_debounceTimer, &QTimer::timeout, this, &DisplayManager::refreshMonitorCache);

    updateCacheInternal();
}

DisplayManager::~DisplayManager() {
    // The application can quit in the middle of a display switch, which is a long blocking OS call that
    // reports back into this object. Shutdown is the only place allowed to wait for it: ordinary use
    // never blocks the GUI thread here.
    QMutexLocker locker(&m_switchMutex);
    while (m_runningSwitches > 0) {
        m_switchFinished.wait(&m_switchMutex);
    }
}

bool DisplayManager::parseMonitorKey(const QString& key, WinLuid& outAdapterId, unsigned int& outTargetId) {
    QStringList parts = key.split('_');

    if (parts.size() != 3) {
        return false;
    }

    bool okLow, okHigh, okTarget;

    outAdapterId.LowPart = parts[0].toULong(&okLow, 16);
    outAdapterId.HighPart = parts[1].toLong(&okHigh, 16);
    outTargetId = parts[2].toUInt(&okTarget, 16);

    return okLow && okHigh && okTarget;
}

QList<MonitorInfo> DisplayManager::getPhysicalMonitors() const {
    QMutexLocker locker(&m_mutex);
    return m_monitorCache;
}

void DisplayManager::updateCacheInternal() {
    QList<MonitorInfo> newList;
    QString newCurrentKey;

    POINT ptZero = {0, 0};
    HMONITOR hMonitor = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    QString primaryGdiName;
    if (GetMonitorInfoW(hMonitor, &mi)) {
        primaryGdiName = QString::fromWCharArray(mi.szDevice); // This will be "\\.\DISPLAY1"
    }

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ALL_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), NULL) != ERROR_SUCCESS) {
        return;
    }

    int displayCounter = 1;

    for (const auto& path : paths) {
        if (!path.targetInfo.targetAvailable)
            continue;

        auto it = std::find_if(newList.begin(), newList.end(), [&](const MonitorInfo& m) {
            return m.adapterId.LowPart == path.targetInfo.adapterId.LowPart && m.targetId == path.targetInfo.id;
        });
        if (it != newList.end())
            continue;

        bool isActive = (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;

        // We get a friendly name (for example, "LG HDR 4K")
        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
        targetName.header.adapterId = path.targetInfo.adapterId;
        targetName.header.id = path.targetInfo.id;

        QString friendlyName = u"Unknown"_s;
        QString devicePathKey;
        if (DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS) {
            friendlyName = QString::fromWCharArray(targetName.monitorFriendlyDeviceName);

            devicePathKey = QString::fromWCharArray(targetName.monitorDevicePath);
        }

        if (friendlyName.isEmpty()) {
            friendlyName = u"Display %1"_s.arg(displayCounter++);
        }

        // Get the GDI system name (for example, "\\.\DISPLAY1")
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
        sourceName.header.adapterId = path.sourceInfo.adapterId;
        sourceName.header.id = path.sourceInfo.id;

        QString displayLabel = "";
        if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
            displayLabel = QString::fromWCharArray(sourceName.viewGdiDeviceName);
        }

        bool isPrimary = (isActive && !displayLabel.isEmpty() && displayLabel == primaryGdiName);
        if (isPrimary) {
            newCurrentKey = devicePathKey;
        }

        // If the GDI name is empty (the monitor is off), we will give it a temporary name later.
        newList.push_back({devicePathKey,
                           WinLuid{path.targetInfo.adapterId.LowPart, path.targetInfo.adapterId.HighPart},
                           path.targetInfo.id, friendlyName, displayLabel, isActive, isPrimary});
    }

    std::sort(newList.begin(), newList.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.displayLabel.isEmpty())
            return false;
        if (b.displayLabel.isEmpty())
            return true;
        return a.displayLabel < b.displayLabel;
    });

    {
        QMutexLocker locker(&m_mutex);
        m_monitorCache = std::move(newList);
        m_currentDisplayKeyCache = newCurrentKey;
    }
}

std::optional<MonitorInfo> DisplayManager::findMonitorByKey(const QString& key) {
    auto monitors = getPhysicalMonitors();
    auto it = std::find_if(monitors.begin(), monitors.end(), [&](const MonitorInfo& m) { return m.key == key; });

    return (it != monitors.end()) ? std::make_optional(*it) : std::nullopt;
}

bool DisplayManager::setDisplayMode(const QString& targetDevicePath) {
    const QString error = applyDisplayConfiguration(targetDevicePath);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return false;
    }

    qCDebug(lcService) << "Successfully switched display";
    QMetaObject::invokeMethod(this, &DisplayManager::onSystemDisplaysChanged, Qt::QueuedConnection);
    return true;
}

QString DisplayManager::applyDisplayConfiguration(const QString& targetDevicePath) {
    UINT32 pathCount = 0, modeCount = 0;
    LONG status;

    // Get buffer sizes (no retry needed - transient errors are rare here)
    status = GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &pathCount, &modeCount);
    if (status != ERROR_SUCCESS) {
        qCWarning(lcService) << "GetDisplayConfigBufferSizes failed:" << status;
        return tr("Failed to query display configuration (error code: %1)").arg(status);
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    // Retry logic for QueryDisplayConfig (transient errors possible during display changes)
    for (int attempt = 0; attempt < 3; ++attempt) {
        status = QueryDisplayConfig(QDC_ALL_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), NULL);
        if (status == ERROR_SUCCESS)
            break;

        QThread::msleep(100);
    }

    if (status != ERROR_SUCCESS) {
        qCWarning(lcService) << "QueryDisplayConfig failed:" << status;
        return tr("Failed to query display paths (error code: %1)").arg(status);
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> targetPaths;

    for (auto& path : paths) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
        targetName.header.adapterId = path.targetInfo.adapterId;
        targetName.header.id = path.targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS) {
            QString currentPathDevicePath = QString::fromWCharArray(targetName.monitorDevicePath);

            if (currentPathDevicePath == targetDevicePath) {
                path.flags = DISPLAYCONFIG_PATH_ACTIVE;
                path.sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
                path.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

                targetPaths.push_back(path);
                break;
            }
        }
    }

    if (targetPaths.empty()) {
        qCWarning(lcService) << "Could not find monitor with Device Path:" << targetDevicePath;
        return tr("Target display not found: %1").arg(targetDevicePath);
    }

    status = SetDisplayConfig((UINT32)targetPaths.size(), targetPaths.data(), 0, NULL,
                              SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES |
                                  /*SDC_FORCE_MODE_ENUMERATION |*/ SDC_SAVE_TO_DATABASE);

    if (status == ERROR_SUCCESS) {
        return {};
    }

    if (status == ERROR_ACCESS_DENIED) {
        qCWarning(lcService) << "Access Denied (Error 5). System might be locked.";
    }

    qCWarning(lcService) << "Failed to apply display configuration. Error code:" << status;
    return tr("Failed to apply display configuration (error code: %1)").arg(status);
}

QFuture<bool> DisplayManager::setDisplayModeAsync(const QString& displayId) {
    {
        QMutexLocker locker(&m_switchMutex);
        ++m_runningSwitches;
    }

    return QtConcurrent::run([this, displayId]() {
        const auto markFinished = qScopeGuard([this]() {
            QMutexLocker locker(&m_switchMutex);
            --m_runningSwitches;
            m_switchFinished.wakeAll();
        });

        return setDisplayMode(displayId);
    });
}

void DisplayManager::onSystemDisplaysChanged() {
    m_debounceTimer->start();
}

void DisplayManager::refreshMonitorCache() {
    updateCacheInternal();
    emit displaysChanged();
}

QString DisplayManager::getCurrentDisplayKey() const {
    QMutexLocker locker(&m_mutex);
    return m_currentDisplayKeyCache;
}
} // namespace ModeFlow::Services
