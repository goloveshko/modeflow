#include "ConfigManager.h"

#include <QSettings>

#include "Constants.h"
#include "FontAwesome.h"
#include "LocalizationManager.h"
#include "Logging.h"

namespace ModeFlow::Core {

using namespace Qt::StringLiterals;

ConfigManager::ConfigManager(QObject* parent) : QObject(parent) {
    m_state.language = LocalizationManager::normalizedLocale(QLocale::system().name());
}

QList<WorkspaceConfig> ConfigManager::getWorkspaces() const {
    QMutexLocker locker(&m_mutex);
    return m_state.workspaces;
}

void ConfigManager::setWorkspaces(const QList<WorkspaceConfig>& list) {
    QMutexLocker locker(&m_mutex);
    m_state.workspaces = list;
}

ConfigState ConfigManager::takeSnapshot() const {
    QMutexLocker locker(&m_mutex);
    return m_state;
}

bool ConfigManager::loadConfig() {
    QSettings s;

    if (s.status() == QSettings::AccessError || s.status() == QSettings::FormatError) {
        emit errorOccurred(tr("Configuration file is corrupted or inaccessible."));
        return false;
    }

    ConfigState snapshot;

    const int size = s.beginReadArray(u"Workspaces"_sv);
    for (int i = 0; i < size; ++i) {
        s.setArrayIndex(i);
        auto hkStr = s.value(u"hotkey"_sv).toString();
        WorkspaceConfig cfg;
        cfg.id = s.value(u"id"_sv).toString();
        cfg.name = s.value(u"name"_sv, tr("Workspace")).toString();
        cfg.iconSymbol = s.value(u"iconSymbol"_sv).toString();
        cfg.hotkey = QKeySequence(hkStr, QKeySequence::PortableText);
        cfg.displayId = s.value(u"displayId"_sv).toString();
        cfg.audioId = s.value(u"audioId"_sv).toString();
        cfg.skipInCycle = s.value(u"skipInCycle"_sv, false).toBool();

        const int appsSize = s.beginReadArray(u"Apps"_sv);
        for (int j = 0; j < appsSize; ++j) {
            s.setArrayIndex(j);
            AppLaunchConfig appCfg;
            appCfg.appPath = s.value(u"appPath"_sv).toString();
            appCfg.delaySeconds = s.value(u"delaySeconds"_sv, 0).toInt();
            appCfg.closeOnExit = s.value(u"closeOnExit"_sv, false).toBool();
            appCfg.delaySeconds = std::clamp(appCfg.delaySeconds, 0, Utils::MaxAppLaunchDelaySeconds);
            cfg.appsToLaunch.append(appCfg);
        }
        s.endArray();

        snapshot.workspaces.append(cfg);
    }
    s.endArray();

    QSet<QString> seenIds;
    for (auto& cfg : snapshot.workspaces) {
        if (cfg.id.isEmpty() || seenIds.contains(cfg.id)) {
            cfg.id = QUuid::createUuid().toString();
        }
        seenIds.insert(cfg.id);
        if (cfg.iconSymbol.isEmpty()) {
            cfg.iconSymbol = Gui::FontAwesome::Desktop;
        }
    }

    auto hkStr = s.value(u"nextProfileHotkey"_sv).toString();
    snapshot.nextProfileHotkey = singleChord(QKeySequence(hkStr, QKeySequence::PortableText));
    snapshot.language =
        LocalizationManager::normalizedLocale(s.value(u"language"_sv, QLocale::system().name()).toString());
    snapshot.theme = static_cast<Theme>(s.value(u"theme"_sv, static_cast<int>(Theme::Light)).toInt());
    snapshot.qtStyleKey = s.value(u"qtStyleKey"_sv, Utils::DefaultQtStyleKey.toString()).toString();
    snapshot.lastActiveProfileId = s.value(u"lastActiveProfileId"_sv).toString();
    snapshot.selectedProfileId = s.value(u"selectedProfileId"_sv).toString();
    snapshot.startupAction = static_cast<StartupAction>(s.value(u"startupAction"_sv, 0).toInt());
    snapshot.startupProfileId = s.value(u"startupProfileId"_sv).toString();
    snapshot.audioConfirmation = s.value(u"audioFeedback"_sv, true).toBool();
    snapshot.autoUpdateEnabled = s.value(u"autoUpdate"_sv, true).toBool();
    snapshot.autostartDelay = s.value(u"autostartDelay"_sv, 5).toInt();
    snapshot.askConfirmation = s.value(u"askConfirmation"_sv, true).toBool();

    snapshot.mainWindowVisible = s.value(u"window/visible"_sv, true).toBool();
    snapshot.mainWindowMaximized = s.value(u"window/maximized"_sv, false).toBool();
    snapshot.mainWindowGeometry = s.value(u"window/geometry"_sv).toRect();

    snapshot.lastUpdateCheckTimestamp = s.value(u"update/lastCheckTimestamp"_sv, 0).toLongLong();
    snapshot.skippedVersion = s.value(u"update/skippedVersion"_sv).toString();

    snapshot.loggingEnabled = s.value(u"loggingEnabled"_sv, false).toBool();

    // Fast atomic move of state snapshot under lock
    {
        QMutexLocker locker(&m_mutex);
        m_state = std::move(snapshot);
    }

    return true;
}

bool ConfigManager::saveConfig() {
    const ConfigState snapshot = takeSnapshot();

    QSettings s;

    s.beginWriteArray("Workspaces");
    for (int i = 0; i < snapshot.workspaces.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(u"id"_sv, snapshot.workspaces[i].id);
        s.setValue(u"name"_sv, snapshot.workspaces[i].name);
        s.setValue(u"iconSymbol"_sv, snapshot.workspaces[i].iconSymbol);
        s.setValue(u"hotkey"_sv, snapshot.workspaces[i].hotkey.toString(QKeySequence::PortableText));
        s.setValue(u"displayId"_sv, snapshot.workspaces[i].displayId);
        s.setValue(u"audioId"_sv, snapshot.workspaces[i].audioId);
        s.setValue(u"skipInCycle"_sv, snapshot.workspaces[i].skipInCycle);

        s.beginWriteArray(u"Apps"_sv, snapshot.workspaces[i].appsToLaunch.size());
        for (int j = 0; j < snapshot.workspaces[i].appsToLaunch.size(); ++j) {
            s.setArrayIndex(j);
            const auto& app = snapshot.workspaces[i].appsToLaunch[j];
            s.setValue(u"appPath"_sv, app.appPath);
            s.setValue(u"delaySeconds"_sv, app.delaySeconds);
            s.setValue(u"closeOnExit"_sv, app.closeOnExit);
        }
        s.endArray();
    }
    s.endArray();

    s.setValue(u"nextProfileHotkey"_sv, snapshot.nextProfileHotkey.toString(QKeySequence::PortableText));
    s.setValue(u"language"_sv, snapshot.language);
    s.setValue(u"theme"_sv, static_cast<int>(snapshot.theme));
    s.setValue(u"qtStyleKey"_sv, snapshot.qtStyleKey);
    s.setValue(u"lastActiveProfileId"_sv, snapshot.lastActiveProfileId);
    s.setValue(u"selectedProfileId"_sv, snapshot.selectedProfileId);
    s.setValue(u"startupAction"_sv, static_cast<int>(snapshot.startupAction));
    s.setValue(u"startupProfileId"_sv, snapshot.startupProfileId);
    s.setValue(u"audioFeedback"_sv, snapshot.audioConfirmation);
    s.setValue(u"autoUpdate"_sv, snapshot.autoUpdateEnabled);
    s.setValue(u"autostartDelay"_sv, snapshot.autostartDelay);
    s.setValue(u"askConfirmation"_sv, snapshot.askConfirmation);

    s.setValue(u"window/visible"_sv, snapshot.mainWindowVisible);
    s.setValue(u"window/maximized"_sv, snapshot.mainWindowMaximized);
    // A window that was never placed has no key at all; writing a default rect would make the next
    // launch believe the user parked it there. The separate pos/size keys are no longer read.
    if (snapshot.mainWindowGeometry.isValid()) {
        s.setValue(u"window/geometry"_sv, snapshot.mainWindowGeometry);
    } else {
        s.remove(u"window/geometry"_sv);
    }
    s.remove(u"window/pos"_sv);
    s.remove(u"window/size"_sv);

    s.setValue(u"update/lastCheckTimestamp"_sv, snapshot.lastUpdateCheckTimestamp);
    s.setValue(u"update/skippedVersion"_sv, snapshot.skippedVersion);

    s.setValue(u"loggingEnabled"_sv, snapshot.loggingEnabled);

    s.sync();

    if (s.status() != QSettings::NoError) {
        qCCritical(lcCore) << "ConfigManager::saveConfig - Failed to write settings:" << s.status();
        emit errorOccurred(tr("Failed to save settings. Check disk space or permissions."));
        return false;
    }

    return true;
}

QKeySequence ConfigManager::nextProfileHotkey() const {
    QMutexLocker locker(&m_mutex);
    return m_state.nextProfileHotkey;
}

void ConfigManager::setNextProfileHotkey(const QKeySequence& key) {
    QMutexLocker locker(&m_mutex);
    m_state.nextProfileHotkey = key;
}

QString ConfigManager::language() const {
    QMutexLocker locker(&m_mutex);
    return m_state.language;
}

void ConfigManager::setLanguage(const QString& locale) {
    QMutexLocker locker(&m_mutex);
    m_state.language = LocalizationManager::normalizedLocale(locale);
}

QString ConfigManager::lastActiveProfileId() const {
    QMutexLocker locker(&m_mutex);
    return m_state.lastActiveProfileId;
}

void ConfigManager::setLastActiveProfileId(const QString& id) {
    QMutexLocker locker(&m_mutex);
    m_state.lastActiveProfileId = id;
}

QString ConfigManager::selectedProfileId() const {
    QMutexLocker locker(&m_mutex);
    return m_state.selectedProfileId;
}

void ConfigManager::setSelectedProfileId(const QString& id) {
    QMutexLocker locker(&m_mutex);
    m_state.selectedProfileId = id;
}

StartupAction ConfigManager::startupAction() const {
    QMutexLocker locker(&m_mutex);
    return m_state.startupAction;
}

void ConfigManager::setStartupAction(StartupAction action) {
    QMutexLocker locker(&m_mutex);
    m_state.startupAction = action;
}

QString ConfigManager::startupProfileId() const {
    QMutexLocker locker(&m_mutex);
    return m_state.startupProfileId;
}

void ConfigManager::setStartupProfileId(const QString& id) {
    QMutexLocker locker(&m_mutex);
    m_state.startupProfileId = id;
}

bool ConfigManager::audioConfirmation() const {
    QMutexLocker locker(&m_mutex);
    return m_state.audioConfirmation;
}

void ConfigManager::setAudioConfirmation(bool enabled) {
    QMutexLocker locker(&m_mutex);
    m_state.audioConfirmation = enabled;
}

bool ConfigManager::autoUpdateEnabled() const {
    QMutexLocker locker(&m_mutex);
    return m_state.autoUpdateEnabled;
}

void ConfigManager::setAutoUpdateEnabled(bool enabled) {
    QMutexLocker locker(&m_mutex);
    m_state.autoUpdateEnabled = enabled;
}

int ConfigManager::autostartDelay() const {
    QMutexLocker locker(&m_mutex);
    return m_state.autostartDelay;
}

void ConfigManager::setAutostartDelay(int seconds) {
    QMutexLocker locker(&m_mutex);
    m_state.autostartDelay = seconds;
}

bool ConfigManager::askConfirmation() const {
    QMutexLocker locker(&m_mutex);
    return m_state.askConfirmation;
}

void ConfigManager::setAskConfirmation(bool enabled) {
    QMutexLocker locker(&m_mutex);
    m_state.askConfirmation = enabled;
}

bool ConfigManager::isMainWindowMaximized() const {
    QMutexLocker locker(&m_mutex);
    return m_state.mainWindowMaximized;
}

void ConfigManager::setMainWindowMaximized(bool maximized) {
    QMutexLocker locker(&m_mutex);
    m_state.mainWindowMaximized = maximized;
}

QRect ConfigManager::mainWindowGeometry() const {
    QMutexLocker locker(&m_mutex);
    return m_state.mainWindowGeometry;
}

void ConfigManager::setMainWindowGeometry(const QRect& geometry) {
    QMutexLocker locker(&m_mutex);
    m_state.mainWindowGeometry = geometry;
}

Theme ConfigManager::currentTheme() const {
    QMutexLocker locker(&m_mutex);
    return m_state.theme;
}

void ConfigManager::setTheme(Theme theme) {
    QMutexLocker locker(&m_mutex);
    m_state.theme = theme;
}

QString ConfigManager::currentQtStyleKey() const {
    QMutexLocker locker(&m_mutex);
    return m_state.qtStyleKey;
}

void ConfigManager::setQtStyleKey(const QString& styleKey) {
    if (!styleKey.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        m_state.qtStyleKey = styleKey;
    }
}

bool ConfigManager::isMainWindowVisible() const {
    QMutexLocker locker(&m_mutex);
    return m_state.mainWindowVisible;
}

void ConfigManager::setMainWindowVisible(bool visible) {
    QMutexLocker locker(&m_mutex);
    m_state.mainWindowVisible = visible;
}

qint64 ConfigManager::lastUpdateCheckTimestamp() const {
    QMutexLocker locker(&m_mutex);
    return m_state.lastUpdateCheckTimestamp;
}

void ConfigManager::setLastUpdateCheckTimestamp(qint64 timestamp) {
    QMutexLocker locker(&m_mutex);
    m_state.lastUpdateCheckTimestamp = timestamp;
}

bool ConfigManager::loggingEnabled() const {
    QMutexLocker locker(&m_mutex);
    return m_state.loggingEnabled;
}

void ConfigManager::setLoggingEnabled(bool enabled) {
    QMutexLocker locker(&m_mutex);
    m_state.loggingEnabled = enabled;
}

QString ConfigManager::skippedVersion() const {
    QMutexLocker locker(&m_mutex);
    return m_state.skippedVersion;
}

void ConfigManager::setSkippedVersion(const QString& version) {
    QMutexLocker locker(&m_mutex);
    m_state.skippedVersion = version;
}

} // namespace ModeFlow::Core
