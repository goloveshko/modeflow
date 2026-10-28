#pragma once

#include <QMutex>
#include <QObject>
#include <QWaitCondition>
#include <QtConcurrent>

namespace ModeFlow::Services {

/**
 * @brief Binary-compatible equivalent of Windows LUID to prevent <windows.h> pollution in headers.
 */
struct WinLuid {
    unsigned long LowPart = 0;
    long HighPart = 0;

    bool operator==(const WinLuid& other) const { return LowPart == other.LowPart && HighPart == other.HighPart; }
};

struct MonitorInfo {
    QString key;
    WinLuid adapterId;
    unsigned int targetId = 0;
    QString friendlyName;
    QString displayLabel;
    bool isActive = false;
    bool isPrimary = false;

    bool operator==(const MonitorInfo& other) const {
        return adapterId == other.adapterId && targetId == other.targetId;
    }
};

class DisplayManager : public QObject {
    Q_OBJECT
public:
    explicit DisplayManager(QObject* parent = nullptr);
    ~DisplayManager() override;

    virtual QFuture<bool> setDisplayModeAsync(const QString& displayId);

    // Using platform-independent types in header
    static bool parseMonitorKey(const QString& key, WinLuid& outAdapterId, unsigned int& outTargetId);

    void refreshMonitorCache();
    QList<MonitorInfo> getPhysicalMonitors() const;

    virtual QString getCurrentDisplayKey() const;

private:
    std::optional<MonitorInfo> findMonitorByKey(const QString& key);
    void updateCacheInternal();
    bool setDisplayMode(const QString& targetDevicePath);

protected:
    /**
     * @brief Blocking part of a switch: talks to the operating system and returns an error instead of
     * reaching back into the manager, so a worker thread can call it without touching object state.
     * @return An empty string on success, otherwise the message for the user.
     */
    virtual QString applyDisplayConfiguration(const QString& displayId);

private:
    QString m_currentDisplayKeyCache;
    QList<MonitorInfo> m_monitorCache;
    mutable QMutex m_mutex;
    QTimer* m_debounceTimer = nullptr;

    // A switch runs off the GUI thread and comes back into this object when it ends, so the destructor
    // has to let running switches finish before the memory goes away.
    QMutex m_switchMutex;
    QWaitCondition m_switchFinished;
    int m_runningSwitches = 0;

signals:
    void displaysChanged();
    void errorOccurred(const QString& message);

private slots:
    void onSystemDisplaysChanged();
};
} // namespace ModeFlow::Services