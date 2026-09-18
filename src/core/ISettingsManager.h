#pragma once

#include <QFuture>

#include "ConfigManager.h"

namespace ModeFlow::Core {

class ISettingsManager {
public:
    virtual ~ISettingsManager() = default;

    virtual QList<LanguageData> availableLanguages() const = 0;
    virtual QString currentLanguage() const = 0;
    virtual void setLanguage(const QString& locale) = 0;
    virtual void setLanguagePreference(const QString& locale) = 0;

    virtual bool autostartEnabled() const = 0;
    virtual QFuture<bool> autostartEnabledAsync() const = 0;
    virtual QFuture<bool> requestAutostartToggleAsync(bool enabled, int delay, bool enableLogging = false) = 0;
    virtual int autostartDelay() const = 0;
    virtual void setAutostartDelay(int seconds) = 0;

    virtual bool audioConfirmation() const = 0;
    virtual void setAudioConfirmation(bool enabled) = 0;
    virtual bool autoUpdateEnabled() const = 0;
    virtual void setAutoUpdateEnabled(bool enabled) = 0;
    virtual QKeySequence nextProfileHotkey() const = 0;
    virtual void setNextProfileHotkey(const QKeySequence& seq) = 0;

    virtual bool mainWindowMaximized() const = 0;
    virtual void setMainWindowMaximized(bool maximized) = 0;
    virtual QPoint mainWindowPos() const = 0;
    virtual void setMainWindowPos(const QPoint& pos) = 0;
    virtual QSize mainWindowSize() const = 0;
    virtual void setMainWindowSize(const QSize& size) = 0;
    virtual bool mainWindowVisible() const = 0;
    virtual void setMainWindowVisible(bool visible) = 0;

    virtual bool loggingEnabled() const = 0;
    virtual void setLoggingEnabled(bool enabled) = 0;

    virtual StartupAction startupAction() const = 0;
    virtual QString startupProfileId() const = 0;
    virtual void setStartupBehavior(StartupAction action, const QString& profileId) = 0;

    virtual bool saveSettings() = 0;

    virtual QList<ThemeData> availableThemes() const = 0;
    virtual Theme currentTheme() const = 0;
    virtual QString currentQtStyleKey() const = 0;
    virtual void setTheme(Theme theme, const QString& qtStyleKey = QString()) = 0;
    virtual void setThemePreference(Theme theme, const QString& qtStyleKey = QString()) = 0;

    virtual bool askConfirmation() const = 0;
    virtual void setAskConfirmation(bool enabled) = 0;

    virtual QString skippedVersion() const = 0;
    virtual void setSkippedVersion(const QString& version) = 0;
};
} // namespace ModeFlow::Core

Q_DECLARE_INTERFACE(ModeFlow::Core::ISettingsManager, "com.ModeFlow.ISettingsManager")
