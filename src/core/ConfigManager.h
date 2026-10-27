#pragma once

#include <QMutex>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>

#include "Constants.h"
#include "WorkspaceModel.h"

namespace ModeFlow::Core {

/**
 * @brief Plain C++ data struct holding application configuration state snapshot.
 */
struct ConfigState {
    QList<WorkspaceConfig> workspaces;

    QKeySequence nextProfileHotkey;
    QString language;
    QString lastActiveProfileId;
    QString selectedProfileId;
    StartupAction startupAction = StartupAction::None;
    QString startupProfileId;
    bool audioConfirmation = true;
    bool autoUpdateEnabled = true;
    int autostartDelay = 0;
    Theme theme = Theme::Light;
    QString qtStyleKey{Utils::DefaultQtStyleKey};
    bool mainWindowVisible = true;

    bool mainWindowMaximized = false;
    // An invalid rect means the user has never had a window to place, which is not the same thing as
    // a saved window sitting at the origin.
    QRect mainWindowGeometry;

    bool askConfirmation = true;
    qint64 lastUpdateCheckTimestamp = 0;

    QString skippedVersion;
    bool loggingEnabled = false;

    bool operator==(const ConfigState&) const = default;
};

class ConfigManager : public QObject {
    Q_OBJECT
public:
    Q_ENUM(StartupAction)

    explicit ConfigManager(QObject* parent = nullptr);

    QList<WorkspaceConfig> getWorkspaces() const;
    void setWorkspaces(const QList<WorkspaceConfig>& list);

    bool loadConfig();
    bool saveConfig();

    QKeySequence nextProfileHotkey() const;
    void setNextProfileHotkey(const QKeySequence& key);

    QString lastActiveProfileId() const;
    void setLastActiveProfileId(const QString& id);

    QString selectedProfileId() const;
    void setSelectedProfileId(const QString& id);

    QString language() const;
    void setLanguage(const QString& locale);

    StartupAction startupAction() const;
    void setStartupAction(StartupAction action);

    QString startupProfileId() const;
    void setStartupProfileId(const QString& id);

    bool audioConfirmation() const;
    void setAudioConfirmation(bool enabled);

    bool autoUpdateEnabled() const;
    void setAutoUpdateEnabled(bool enabled);

    int autostartDelay() const;
    void setAutostartDelay(int seconds);

    bool askConfirmation() const;
    void setAskConfirmation(bool enabled);

    bool isMainWindowMaximized() const;
    void setMainWindowMaximized(bool maximized);

    QRect mainWindowGeometry() const;
    void setMainWindowGeometry(const QRect& geometry);

    Theme currentTheme() const;
    void setTheme(Theme theme);

    QString currentQtStyleKey() const;
    void setQtStyleKey(const QString& styleKey);

    bool isMainWindowVisible() const;
    void setMainWindowVisible(bool visible);

    qint64 lastUpdateCheckTimestamp() const;
    void setLastUpdateCheckTimestamp(qint64 timestamp);

    bool loggingEnabled() const;
    void setLoggingEnabled(bool enabled);

    QString skippedVersion() const;
    void setSkippedVersion(const QString& version);

private:
    ConfigState takeSnapshot() const;

signals:
    void errorOccurred(const QString& message);

private:
    mutable QMutex m_mutex;
    ConfigState m_state;
};

} // namespace ModeFlow::Core
