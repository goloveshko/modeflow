#pragma once

#include <QJsonObject>
#include <QKeySequence>
#include <QList>
#include <QMetaType>

namespace ModeFlow::Core {

struct DeviceEntry {
    QString id;
    QString name;
    bool isConnected = false; ///< detected right now, so the device can be picked
    bool isActive = false;    ///< the device the system is currently using (active display, default endpoint)
};

struct AppLaunchConfig {
    QString appPath;
    int delaySeconds = 0;
    bool closeOnExit = false;

    QJsonObject toJson() const;
    static AppLaunchConfig fromJson(const QJsonObject& obj);
};

struct WorkspaceConfig {
    QString id;
    QString name = "Workspace";
    QString iconSymbol;
    QKeySequence hotkey;
    QString displayId;
    QString audioId;
    bool skipInCycle = false;
    QList<AppLaunchConfig> appsToLaunch;

    QJsonObject toJson() const;
    static WorkspaceConfig fromJson(const QJsonObject& obj);

    bool operator==(const WorkspaceConfig& other) const { return id == other.id; }
};

enum class ActiveDialog { None = 0, Settings, About, LogViewer, Update };

struct LanguageData {
    QString label;
    QString code;
};

enum class StartupAction { None = 0, LastActive = 1, Specific = 2 };

enum class Theme { Dark, Light, Qt };

struct ThemeData {
    QString displayName;
    Theme theme;
    QString styleKey;
    bool isSeparator = false;
};

} // namespace ModeFlow::Core

Q_DECLARE_METATYPE(ModeFlow::Core::DeviceEntry)
Q_DECLARE_METATYPE(ModeFlow::Core::AppLaunchConfig)
Q_DECLARE_METATYPE(QList<ModeFlow::Core::WorkspaceConfig>)
Q_DECLARE_METATYPE(ModeFlow::Core::WorkspaceConfig)
Q_DECLARE_METATYPE(ModeFlow::Core::ActiveDialog)
Q_DECLARE_METATYPE(ModeFlow::Core::StartupAction)
Q_DECLARE_METATYPE(ModeFlow::Core::Theme)
