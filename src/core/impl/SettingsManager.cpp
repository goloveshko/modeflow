#include "SettingsManager.h"

#include "AutostartManager.h"
#include "ConfigManager.h"
#include "LocalizationManager.h"
#include "StyleManager.h"

namespace ModeFlow::Core {

SettingsManager::SettingsManager(ConfigManager* configManager, Services::AutostartManager* autostartManager,
                                 LocalizationManager* localizationManager, StyleManager* styleManager)
    : m_configManager(configManager), m_autostartManager(autostartManager), m_localizationManager(localizationManager),
      m_styleManager(styleManager) {
    Q_ASSERT(m_configManager);
    Q_ASSERT(m_autostartManager);
    Q_ASSERT(m_localizationManager);
    Q_ASSERT(m_styleManager);
}

QList<LanguageData> SettingsManager::availableLanguages() const {
    return m_localizationManager->availableLanguages();
}

QString SettingsManager::currentLanguage() const {
    return m_configManager->language();
}

void SettingsManager::setLanguage(const QString& locale) {
    m_configManager->setLanguage(locale);
    m_localizationManager->switchLanguage(locale);
}

void SettingsManager::setLanguagePreference(const QString& locale) {
    m_configManager->setLanguage(locale);
}

bool SettingsManager::autostartEnabled() const {
    return m_autostartManager->isAutostartEnabled();
}

QFuture<bool> SettingsManager::autostartEnabledAsync() const {
    return m_autostartManager->checkIsRegisteredAsync();
}

QFuture<bool> SettingsManager::requestAutostartToggleAsync(bool enabled, int delay, bool enableLogging) {
    return m_autostartManager->toggleAsync(enabled, delay, enableLogging);
}

int SettingsManager::autostartDelay() const {
    return m_configManager->autostartDelay();
}

void SettingsManager::setAutostartDelay(int seconds) {
    m_configManager->setAutostartDelay(seconds);
}

bool SettingsManager::audioConfirmation() const {
    return m_configManager->audioConfirmation();
}

void SettingsManager::setAudioConfirmation(bool enabled) {
    m_configManager->setAudioConfirmation(enabled);
}

bool SettingsManager::autoUpdateEnabled() const {
    return m_configManager->autoUpdateEnabled();
}

void SettingsManager::setAutoUpdateEnabled(bool enabled) {
    m_configManager->setAutoUpdateEnabled(enabled);
}

QKeySequence SettingsManager::nextProfileHotkey() const {
    return m_configManager->nextProfileHotkey();
}

void SettingsManager::setNextProfileHotkey(const QKeySequence& seq) {
    m_configManager->setNextProfileHotkey(seq);
}

StartupAction SettingsManager::startupAction() const {
    return m_configManager->startupAction();
}

QString SettingsManager::startupProfileId() const {
    return m_configManager->startupProfileId();
}

void SettingsManager::setStartupBehavior(StartupAction action, const QString& profileId) {
    m_configManager->setStartupAction(action);
    m_configManager->setStartupProfileId(profileId);
}

bool SettingsManager::saveSettings() {
    return m_configManager->saveConfig();
}

Theme SettingsManager::currentTheme() const {
    return m_configManager->currentTheme();
}

QString SettingsManager::currentQtStyleKey() const {
    return m_configManager->currentQtStyleKey();
}

void SettingsManager::setTheme(Theme theme, const QString& qtStyleKey) {
    m_styleManager->setTheme(theme, qtStyleKey);
    m_configManager->setTheme(theme);
    m_configManager->setQtStyleKey(m_styleManager->currentQtStyleKey());
}

void SettingsManager::setThemePreference(Theme theme, const QString& qtStyleKey) {
    m_configManager->setTheme(theme);
    m_configManager->setQtStyleKey(qtStyleKey);
}

QList<Core::ThemeData> SettingsManager::availableThemes() const {
    return m_styleManager->availableThemes();
}

bool SettingsManager::mainWindowMaximized() const {
    return m_configManager->isMainWindowMaximized();
}

void SettingsManager::setMainWindowMaximized(bool maximized) {
    m_configManager->setMainWindowMaximized(maximized);
}

QPoint SettingsManager::mainWindowPos() const {
    return m_configManager->mainWindowPos();
}

void SettingsManager::setMainWindowPos(const QPoint& pos) {
    m_configManager->setMainWindowPos(pos);
}

QSize SettingsManager::mainWindowSize() const {
    return m_configManager->mainWindowSize();
}

void SettingsManager::setMainWindowSize(const QSize& size) {
    m_configManager->setMainWindowSize(size);
}

bool SettingsManager::mainWindowVisible() const {
    return m_configManager->isMainWindowVisible();
}

void SettingsManager::setMainWindowVisible(bool visible) {
    m_configManager->setMainWindowVisible(visible);
}

bool SettingsManager::loggingEnabled() const {
    return m_configManager->loggingEnabled();
}

void SettingsManager::setLoggingEnabled(bool enabled) {
    m_configManager->setLoggingEnabled(enabled);
}

bool SettingsManager::askConfirmation() const {
    return m_configManager->askConfirmation();
}

void SettingsManager::setAskConfirmation(bool enabled) {
    m_configManager->setAskConfirmation(enabled);
}

QString SettingsManager::skippedVersion() const {
    return m_configManager->skippedVersion();
}

void SettingsManager::setSkippedVersion(const QString& version) {
    m_configManager->setSkippedVersion(version);
}

} // namespace ModeFlow::Core
