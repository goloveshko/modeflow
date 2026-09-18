#pragma once

#include "ISettingsManager.h"

namespace ModeFlow::Services {
class AutostartManager;
} // namespace ModeFlow::Services

namespace ModeFlow::Core {

class ConfigManager;
class LocalizationManager;
class StyleManager;

class SettingsManager : public ISettingsManager {
public:
    SettingsManager(ConfigManager* configManager, Services::AutostartManager* autostartManager,
                    LocalizationManager* localizationManager, StyleManager* styleManager);

    QList<LanguageData> availableLanguages() const override;
    QString currentLanguage() const override;
    void setLanguage(const QString& locale) override;
    void setLanguagePreference(const QString& locale) override;

    bool autostartEnabled() const override;
    QFuture<bool> autostartEnabledAsync() const override;
    QFuture<bool> requestAutostartToggleAsync(bool enabled, int delay, bool enableLogging = false) override;
    int autostartDelay() const override;
    void setAutostartDelay(int seconds) override;

    bool audioConfirmation() const override;
    void setAudioConfirmation(bool enabled) override;

    bool autoUpdateEnabled() const override;
    void setAutoUpdateEnabled(bool enabled) override;

    QKeySequence nextProfileHotkey() const override;
    void setNextProfileHotkey(const QKeySequence& seq) override;

    StartupAction startupAction() const override;
    QString startupProfileId() const override;
    void setStartupBehavior(StartupAction action, const QString& profileId) override;
    bool saveSettings() override;

    Theme currentTheme() const override;
    QString currentQtStyleKey() const override;
    void setTheme(Theme theme, const QString& qtStyleKey = QString()) override;
    void setThemePreference(Theme theme, const QString& qtStyleKey = QString()) override;

    QList<ThemeData> availableThemes() const override;

    bool mainWindowMaximized() const override;
    void setMainWindowMaximized(bool maximized) override;
    QPoint mainWindowPos() const override;
    void setMainWindowPos(const QPoint& pos) override;
    QSize mainWindowSize() const override;
    void setMainWindowSize(const QSize& size) override;
    bool mainWindowVisible() const override;
    void setMainWindowVisible(bool visible) override;

    bool loggingEnabled() const override;
    void setLoggingEnabled(bool enabled) override;

    bool askConfirmation() const override;
    void setAskConfirmation(bool enabled) override;

    QString skippedVersion() const override;
    void setSkippedVersion(const QString& version) override;

private:
    ConfigManager* m_configManager;
    Services::AutostartManager* m_autostartManager;
    LocalizationManager* m_localizationManager;
    StyleManager* m_styleManager;
};

} // namespace ModeFlow::Core
