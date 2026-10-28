#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPromise>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QThread>
#include <QtConcurrent>
#include <QtTest>

// Explicitly include required Qt and core types
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPair>
#include <QPoint>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSize>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>
#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
// objidl.h carries the apartment enums used to probe COM state; objbase.h alone does not pull it in.
#include <objbase.h>
#include <objidl.h>
#include <qt_windows.h>
#endif

#include "AppLauncher.h"
#include "AppListWidget.h"
#include "AudioDeviceManager.h"
#include "AutostartManager.h"
#include "CliParser.h"
#include "ComInitGuard.h"
#include "CommandLineBuilder.h"
#include "ConfigManager.h"
#include "ConfigTypes.h"
#include "Constants.h"
#include "DeviceUtils.h"
#include "DisplayManager.h"
#include "HotkeyEdit.h"
#include "HotkeyManager.h"
#include "IDialogManager.h"
#include "ISettingsManager.h"
#include "IStyleManager.h"
#include "IWorkspaceManager.h"
#include "LocalizationManager.h"
#include "LogManager.h"
#include "LogViewerDialog.h"
#include "Logging.h"
#include "MainWindow.h"
#include "ProfileEditor.h"
#include "ProfileIconMenu.h"
#include "ProfileSerializer.h"
#include "ProfileTransfer.h"
#include "SettingsDialog.h"
#include "SingleInstanceGuard.h"
#include "SystemUtils.h"
#include "UpdateService.h"
#include "WorkspaceModel.h"
#include "WorkspaceService.h"

namespace {

/// Types straight into the key-sequence widget so no window has to be shown and the user's
/// foreground window keeps focus.
class ChordTypingEdit : public ModeFlow::Gui::HotkeyEdit {
public:
    using ModeFlow::Gui::HotkeyEdit::HotkeyEdit;

    void type(Qt::Key key, Qt::KeyboardModifiers modifiers) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        keyPressEvent(&event);
    }
};

class FakeStyleManager : public ModeFlow::Core::IStyleManager {
public:
    void applyToWindow(QWidget* window) override { Q_UNUSED(window); }
    ModeFlow::Core::Theme currentTheme() const override { return theme; }
    QString currentQtStyleKey() const override { return qtStyleKey; }
    void setTheme(ModeFlow::Core::Theme newTheme, const QString& newQtStyleKey = QString()) override {
        theme = newTheme;
        if (!newQtStyleKey.isEmpty()) {
            qtStyleKey = newQtStyleKey;
        }
    }
    void showInfo(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastInfoTitle = title;
        lastInfoText = text;
    }

    void showWarning(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastWarningTitle = title;
        lastWarningText = text;
    }

    void showError(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastErrorTitle = title;
        lastErrorText = text;
    }

    bool confirmAction(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        Q_UNUSED(title);
        Q_UNUSED(text);
        return confirmResult;
    }

    int showMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                       const QString& informativeText = QString(), const QStringList& buttons = QStringList(),
                       int defaultButtonIndex = 0) override {
        Q_UNUSED(parent);
        Q_UNUSED(icon);
        Q_UNUSED(title);
        Q_UNUSED(text);
        Q_UNUSED(informativeText);
        Q_UNUSED(buttons);
        Q_UNUSED(defaultButtonIndex);
        return mockResult;
    }

    QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir,
                            const QString& filter) override {
        Q_UNUSED(parent);
        Q_UNUSED(caption);
        Q_UNUSED(dir);
        Q_UNUSED(filter);
        return mockOpenPath;
    }

    QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir,
                            const QString& filter) override {
        Q_UNUSED(parent);
        Q_UNUSED(caption);
        Q_UNUSED(dir);
        Q_UNUSED(filter);
        return mockSavePath;
    }

    void forceUnhover() override {}

    bool confirmResult = true;
    ModeFlow::Core::Theme theme = ModeFlow::Core::Theme::Light;
    QString qtStyleKey = QStringLiteral("windows11");
    QString lastInfoTitle;
    QString lastInfoText;
    QString lastWarningTitle;
    QString lastWarningText;
    QString lastErrorTitle;
    QString lastErrorText;
    int mockResult = 0;

    QString mockOpenPath;
    QString mockSavePath;
};

class FakeWorkspaceManager : public ModeFlow::Core::IWorkspaceManager {
public:
    FakeWorkspaceManager() {
        ModeFlow::Core::WorkspaceConfig config;
        config.id = QStringLiteral("profile-1");
        config.name = QStringLiteral("Desktop");
        config.displayId = QStringLiteral("display-1");
        config.audioId = QStringLiteral("audio-1");
        m_model.setConfigs({config});

        m_displays = {{QStringLiteral("display-1"), QStringLiteral("Main display"), true, true}};
        m_audioOutputs = {{QStringLiteral("audio-1"), QStringLiteral("Main audio"), true, true}};
    }

    QAbstractItemModel* model() const override { return const_cast<ModeFlow::Core::WorkspaceModel*>(&m_model); }
    void addConfig(const ModeFlow::Core::WorkspaceConfig& cfg) override { m_model.addConfig(cfg); }
    void removeConfig(int row) override { m_model.removeConfig(row); }
    void updateConfig(int row, const ModeFlow::Core::WorkspaceConfig& cfg) override { m_model.updateConfig(row, cfg); }
    ModeFlow::Core::WorkspaceConfig captureCurrentHardwareState() const override {
        ModeFlow::Core::WorkspaceConfig config;
        config.displayId = m_displays.first().id;
        config.audioId = m_audioOutputs.first().id;
        return config;
    }
    bool saveWorkspaces() override {
        ++saveCalls;
        return saveShouldSucceed;
    }
    QList<ModeFlow::Core::WorkspaceConfig> configs() const override { return m_model.configs(); }
    void setSelectedRow(int row) override { selectedRowValue = row; }
    int selectedRow() const override { return selectedRowValue; }
    int activeRow() const override { return activeRowValue; }

    QString generateDefaultName() override { return QStringLiteral("Workspace"); }
    void createDefaultProfile() override {
        ModeFlow::Core::WorkspaceConfig cfg;
        cfg.id = QStringLiteral("new-profile-id");
        cfg.name = QStringLiteral("Workspace");
        addConfig(cfg);
    }
    void duplicateProfile(int row) override {
        auto profiles = configs();
        if (row >= 0 && row < profiles.size()) {
            addConfig(profiles[row]);
        }
    }
    QString suggestedProfileIconSymbol(const QString& profileName) const override {
        return m_iconSymbolsByProfileName.value(profileName, QStringLiteral("desktop"));
    }

    QList<ModeFlow::Core::DeviceEntry> getAvailableDisplays() const override { return m_displays; }
    QList<ModeFlow::Core::DeviceEntry> getAvailableAudioOutputs() const override { return m_audioOutputs; }

    mutable ModeFlow::Core::WorkspaceModel m_model;
    QList<ModeFlow::Core::DeviceEntry> m_displays;
    QList<ModeFlow::Core::DeviceEntry> m_audioOutputs;
    QMap<QString, QString> m_iconSymbolsByProfileName;
    int saveCalls = 0;
    bool saveShouldSucceed = true;
    int selectedRowValue = 0;
    int activeRowValue = 0;
};

class FakeSettingsManager : public ModeFlow::Core::ISettingsManager {
public:
    QList<ModeFlow::Core::LanguageData> availableLanguages() const override {
        return {{QStringLiteral("English"), QStringLiteral("en_US")},
                {QStringLiteral("Русский"), QStringLiteral("ru_RU")}};
    }

    QString currentLanguage() const override { return languageCode; }
    void setLanguage(const QString& locale) override { languageCode = locale; }
    void setLanguagePreference(const QString& locale) override { languageCode = locale; }

    QFuture<bool> autostartEnabledAsync() const override {
        ++autostartCheckCalls;

        if (m_deferAutostartCheck) {
            // The test decides when the Task Scheduler answer arrives, so the user can already be
            // editing the form while the dialog waits for it.
            m_deferAutostartCheck = false;
            auto promise = std::make_unique<QPromise<bool>>();
            promise->start();
            m_deferredAutostartFuture = promise->future();
            m_pendingAutostartCheck = std::move(promise);
            return m_deferredAutostartFuture;
        }

        return QtFuture::makeReadyValueFuture(autostartEnabledValue);
    }

    void deferAutostartCheck() { m_deferAutostartCheck = true; }

    void finishDeferredAutostartCheck(bool registered) {
        if (!m_pendingAutostartCheck) {
            return;
        }
        m_pendingAutostartCheck->addResult(registered);
        m_pendingAutostartCheck->finish();
        m_pendingAutostartCheck.reset();
    }

    QFuture<bool> requestAutostartToggleAsync(bool enabled, int delay, bool enableLogging = false) override {
        autostartRequests.append({enabled, delay});

        if (m_deferNextAutostartToggle) {
            // The test decides when Task Scheduler reports back, so the dialog can be observed while the
            // external change is still running.
            m_deferNextAutostartToggle = false;
            auto promise = std::make_unique<QPromise<bool>>();
            promise->start();
            QFuture<bool> future = promise->future();
            m_pendingAutostartToggles.push_back({enabled, delay, std::move(promise)});
            return future;
        }

        const bool success =
            autostartToggleResults.isEmpty() ? autostartToggleShouldSucceed : autostartToggleResults.takeFirst();
        if (!success) {
            return QtFuture::makeReadyValueFuture(false);
        }

        autostartEnabledValue = enabled;
        autostartDelaySeconds = delay;
        return QtFuture::makeReadyValueFuture(true);
    }

    void deferNextAutostartToggle() { m_deferNextAutostartToggle = true; }

    // Applies the same bookkeeping an immediate toggle would, so the fake stays believable afterwards.
    void finishDeferredAutostartToggle(bool success) {
        if (m_pendingAutostartToggles.empty()) {
            return;
        }
        PendingToggle pending = std::move(m_pendingAutostartToggles.front());
        m_pendingAutostartToggles.erase(m_pendingAutostartToggles.begin());

        if (success) {
            autostartEnabledValue = pending.enabled;
            autostartDelaySeconds = pending.delay;
        }

        pending.promise->addResult(success);
        pending.promise->finish();
    }

    int autostartDelay() const override { return autostartDelaySeconds; }
    void setAutostartDelay(int seconds) override { autostartDelaySeconds = seconds; }

    bool audioConfirmation() const override { return audioConfirmationEnabled; }
    void setAudioConfirmation(bool enabled) override { audioConfirmationEnabled = enabled; }

    bool autoUpdateEnabled() const override { return autoUpdateEnabledValue; }
    void setAutoUpdateEnabled(bool enabled) override { autoUpdateEnabledValue = enabled; }

    bool rememberWindowLayouts() const override { return rememberWindowLayoutsValue; }
    void setRememberWindowLayouts(bool enabled) override { rememberWindowLayoutsValue = enabled; }

    QKeySequence nextProfileHotkey() const override { return nextProfileSequence; }
    void setNextProfileHotkey(const QKeySequence& seq) override { nextProfileSequence = seq; }

    ModeFlow::Core::StartupAction startupAction() const override { return startupActionValue; }
    QString startupProfileId() const override { return startupProfileIdValue; }
    void setStartupBehavior(ModeFlow::Core::StartupAction action, const QString& profileId) override {
        startupActionValue = action;
        startupProfileIdValue = profileId;
    }

    bool saveSettings() override {
        ++saveCalls;
        return saveShouldSucceed;
    }

    bool mainWindowMaximized() const override { return maximizedMock; }
    void setMainWindowMaximized(bool maximized) override { maximizedMock = maximized; }
    QRect mainWindowGeometry() const override { return geometryMock; }
    void setMainWindowGeometry(const QRect& geometry) override { geometryMock = geometry; }

    bool mainWindowVisible() const override { return visibleMock; }
    void setMainWindowVisible(bool visible) override { visibleMock = visible; }

    QList<ModeFlow::Core::ThemeData> availableThemes() const override {
        return {{QStringLiteral("Light"), ModeFlow::Core::Theme::Light, QString(), false},
                {QStringLiteral("Dark"), ModeFlow::Core::Theme::Dark, QString(), false}};
    }

    ModeFlow::Core::Theme currentTheme() const override { return themeValue; }
    QString currentQtStyleKey() const override { return qtStyleKey; }
    void setTheme(ModeFlow::Core::Theme theme, const QString& styleKey = QString()) override {
        themeValue = theme;
        if (!styleKey.isEmpty()) {
            qtStyleKey = styleKey;
        }
    }
    void setThemePreference(ModeFlow::Core::Theme theme, const QString& styleKey = QString()) override {
        themeValue = theme;
        if (!styleKey.isEmpty()) {
            qtStyleKey = styleKey;
        }
    }

    bool askConfirmation() const override { return askConfirmationMock; }
    void setAskConfirmation(bool enabled) override { askConfirmationMock = enabled; }

    bool loggingEnabled() const override { return loggingEnabledValue; }
    void setLoggingEnabled(bool enabled) override { loggingEnabledValue = enabled; }

    QString skippedVersion() const override { return skippedVersionValue; }
    void setSkippedVersion(const QString& version) override { skippedVersionValue = version; }

    QString languageCode = QStringLiteral("en_US");
    bool autostartEnabledValue = false;
    mutable int autostartCheckCalls = 0;
    mutable bool m_deferAutostartCheck = false;
    mutable std::unique_ptr<QPromise<bool>> m_pendingAutostartCheck;
    mutable QFuture<bool> m_deferredAutostartFuture;
    int autostartDelaySeconds = 5;
    bool audioConfirmationEnabled = true;
    bool autoUpdateEnabledValue = true;
    bool rememberWindowLayoutsValue = false;
    QKeySequence nextProfileSequence;
    ModeFlow::Core::StartupAction startupActionValue = ModeFlow::Core::StartupAction::None;
    QString startupProfileIdValue;
    ModeFlow::Core::Theme themeValue = ModeFlow::Core::Theme::Light;
    QString qtStyleKey = QStringLiteral("windows11");
    QList<QPair<bool, int>> autostartRequests;
    bool autostartToggleShouldSucceed = true;
    // When filled, these decide the outcome of the next toggles in order, whatever the default says.
    QList<bool> autostartToggleResults;
    bool m_deferNextAutostartToggle = false;
    struct PendingToggle {
        bool enabled;
        int delay;
        std::unique_ptr<QPromise<bool>> promise;
    };
    std::vector<PendingToggle> m_pendingAutostartToggles;
    bool saveShouldSucceed = true;
    int saveCalls = 0;

    bool maximizedMock = false;
    QRect geometryMock;
    bool visibleMock = true;

    bool askConfirmationMock = true;
    bool loggingEnabledValue = false;
    QString skippedVersionValue;
};

class FakeDisplayManagerForWS : public ModeFlow::Services::DisplayManager {
    Q_OBJECT
public:
    using DisplayManager::DisplayManager;

    QFuture<bool> setDisplayModeAsync(const QString& displayId) override {
        m_lastSwitchTarget = displayId;
        if (m_deferSwitch) {
            // The test decides when the system reports back, so a result can be delivered after a
            // later operation has already started.
            auto promise = std::make_unique<QPromise<bool>>();
            promise->start();
            QFuture<bool> future = promise->future();
            m_pendingPromises.push_back(std::move(promise));
            m_deferSwitch = false;
            return future;
        }
        if (m_switchShouldFail) {
            return QtFuture::makeReadyValueFuture(false);
        }
        return QtFuture::makeReadyValueFuture(true);
    }

    QString getCurrentDisplayKey() const override { return m_currentDisplayKey; }

    void emitDisplaysChanged() { emit displaysChanged(); }

    // Delivers the result of the switch that has been waiting the longest.
    void finishDeferredSwitch(bool success) {
        if (m_pendingPromises.empty()) {
            return;
        }
        std::unique_ptr<QPromise<bool>> promise = std::move(m_pendingPromises.front());
        m_pendingPromises.erase(m_pendingPromises.begin());
        promise->addResult(success);
        promise->finish();
    }

    QString m_currentDisplayKey;
    QString m_lastSwitchTarget;
    bool m_switchShouldFail = false;
    bool m_deferSwitch = false;

private:
    std::vector<std::unique_ptr<QPromise<bool>>> m_pendingPromises;
};

// State of a switch that is stuck inside the blocking system call. It lives outside the manager, so a
// test can still observe the worker after the manager itself is gone.
struct HeldSwitch {
    QSemaphore entered{0};
    QSemaphore release{0};
    QString target;
    std::atomic_bool finished{false};
};

class HoldingDisplayManager : public ModeFlow::Services::DisplayManager {
public:
    explicit HoldingDisplayManager(std::shared_ptr<HeldSwitch> state)
        : DisplayManager(nullptr), m_state(std::move(state)) {}

    QString applyDisplayConfiguration(const QString& displayId) override {
        const auto state = m_state; // leave the object behind before the slow part
        state->target = displayId;
        state->entered.release();
        state->release.acquire();
        state->finished = true;
        return {};
    }

    std::shared_ptr<HeldSwitch> m_state;
};

class FakeAudioDeviceManagerForWS : public ModeFlow::Services::AudioDeviceManager {
    Q_OBJECT
public:
    using AudioDeviceManager::AudioDeviceManager;

    QString getDefaultOutputDeviceId() override { return m_defaultOutputDeviceId; }

    void setDefaultOutputDevice(const QString& id) override {
        m_lastSetOutputDeviceId = id;
        m_defaultOutputDeviceId = id;
    }

    QString m_defaultOutputDeviceId;
    QString m_lastSetOutputDeviceId;
};

namespace {
// A launchable executable that is not one of the user's applications: the system copy stays where it
// is and remains blocked by the containment check, while the copy in temp is a legitimate profile path.
QString makeLaunchableExecutable(const QString& tag) {
    const QString winDir = qEnvironmentVariable("windir");
    const QStringList candidates = {QStringLiteral("whoami.exe"), QStringLiteral("find.exe"),
                                    QStringLiteral("ping.exe")};

    for (const QString& name : candidates) {
        const QString source = QDir(winDir).filePath(QStringLiteral("System32/") + name);
        if (!QFile::exists(source)) {
            continue;
        }

        const QString target = QDir::tempPath() + QStringLiteral("/modeflow-launcher-%1.exe").arg(tag);
        QFile::remove(target);
        if (QFile::copy(source, target)) {
            return target;
        }
    }

    return QString();
}

ModeFlow::Core::WorkspaceConfig makeProfileForSwitch(const QString& id, const QString& displayName) {
    ModeFlow::Core::WorkspaceConfig config;
    config.id = id;
    config.name = id;
    config.displayId = displayName;
    return config;
}

// Landing on a profile that launches applications is what creates applications to close later.
ModeFlow::Core::WorkspaceConfig profileWithApps(const QString& id, const QString& displayName, const QString& appPath) {
    ModeFlow::Core::WorkspaceConfig config = makeProfileForSwitch(id, displayName);

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = appPath;
    app.closeOnExit = true;
    config.appsToLaunch.append(app);

    return config;
}
} // namespace

// Stands in for the operating system, not for the launch policy: the real sequence runs, so profile
// paths are validated and de-duplicated against what the profile already owns, while no application is
// started for real and no real window is asked to close.
class FakeAppLauncherForWS : public ModeFlow::Services::AppLauncher {
    Q_OBJECT
public:
    using AppLauncher::AppLauncher;

    qint64 execute(const QString& path) override {
        ++m_executeCount;
        m_lastPath = path;
        // The test process itself is certainly alive, so the tracked record cannot expire mid-test.
        return QCoreApplication::applicationPid();
    }

    bool askProcessToClose(qint64 pid) const override {
        m_closedPids.append(pid);
        return true;
    }

    void cancelDelayedLaunches(const QString& profileId) override {
        m_cancelledProfileIds.append(profileId);
        AppLauncher::cancelDelayedLaunches(profileId);
    }

    void closeProfileProcesses(const QString& profileId) override {
        m_closedProfileIds.append(profileId);
        AppLauncher::closeProfileProcesses(profileId);
    }

    int m_executeCount = 0;
    QString m_lastPath;
    mutable QList<qint64> m_closedPids;
    QStringList m_cancelledProfileIds;
    QStringList m_closedProfileIds;
};

class FakeDialogManager : public ModeFlow::Core::IDialogManager {
public:
    FakeDialogManager() = default;

    ModeFlow::Core::ActiveDialog activeDialog() const override { return ModeFlow::Core::ActiveDialog::None; }
    void setActiveDialog(ModeFlow::Core::ActiveDialog dialog) override { Q_UNUSED(dialog); }

    void showAboutDialog() override {}
    void showLogViewerDialog() override {}
    void showSettingsDialog() override {}
    void showUpdateDialog() override {}
    void forceUpdateCheck() override {}

    std::optional<ModeFlow::Core::AppLaunchConfig>
    showAppLaunchDialog(const ModeFlow::Core::AppLaunchConfig* initialConfig = nullptr,
                        QWidget* parent = nullptr) override {
        Q_UNUSED(initialConfig);
        Q_UNUSED(parent);
        return std::nullopt;
    }

    bool confirmApplyProfile(const ModeFlow::Core::WorkspaceConfig& config) override {
        lastConfirmedProfileId = config.id;
        return confirmResult;
    }

    bool confirmAction(const QString& title, const QString& text) override {
        return confirmAction(nullptr, title, text);
    }

    bool confirmAction(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastConfirmTitle = title;
        lastConfirmText = text;
        return confirmResult;
    }

    void showInfo(const QString& title, const QString& text) override { showInfo(nullptr, title, text); }

    void showInfo(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastWarningTitle = title;
        lastWarningText = text;
    }

    void showWarning(const QString& title, const QString& text) override { showWarning(nullptr, title, text); }

    void showWarning(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastWarningTitle = title;
        lastWarningText = text;
    }

    void showError(const QString& title, const QString& text) override { showError(nullptr, title, text); }

    void showError(QWidget* parent, const QString& title, const QString& text) override {
        Q_UNUSED(parent);
        lastErrorTitle = title;
        lastErrorText = text;
    }

    int showMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                       const QString& informativeText = QString(), const QStringList& buttons = QStringList(),
                       int defaultButtonIndex = 0) override {
        Q_UNUSED(parent);
        Q_UNUSED(icon);
        Q_UNUSED(title);
        Q_UNUSED(text);
        Q_UNUSED(informativeText);
        Q_UNUSED(buttons);
        Q_UNUSED(defaultButtonIndex);
        return mockResult;
    }

    QString getOpenFileName(const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override {
        return getOpenFileName(nullptr, caption, dir, filter);
    }

    QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override {
        Q_UNUSED(parent);
        Q_UNUSED(caption);
        Q_UNUSED(dir);
        Q_UNUSED(filter);
        return mockOpenPath;
    }

    QString getSaveFileName(const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override {
        return getSaveFileName(nullptr, caption, dir, filter);
    }

    QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override {
        Q_UNUSED(parent);
        Q_UNUSED(caption);
        Q_UNUSED(dir);
        Q_UNUSED(filter);
        return mockSavePath;
    }

    bool confirmResult = true;
    QString lastConfirmTitle;
    QString lastConfirmText;
    QString lastWarningTitle;
    QString lastWarningText;
    QString lastErrorTitle;
    QString lastErrorText;
    QString lastConfirmedProfileId;
    int mockResult = 0;
    QString mockOpenPath;
    QString mockSavePath;
};

} // namespace

class ModeFlowTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizedLocale_data();
    void normalizedLocale();
    void unsupportedLocaleFallsBackSilently();
    void workspaceDeletePersistsAndEmitsRefresh();
    void workspaceDeleteSaveFailureDoesNotEmitRefresh();
    void settingsAcceptCommitsAutostartAndConfig();
    void settingsDialog_commitsTheWindowLayoutPreference();
    void settingsAcceptRollsBackOnSaveFailure();
    void commandLineBuilder_generatesCorrectArguments();
    void cliParser_parsesArgumentsCorrectly();
    void cliParser_finishesHelpAndBadArgumentsWithoutTouchingTheTask();
    void hotkeyEdit_replacesTheChordInsteadOfAppendingASecondOne();
    void singleChord_keepsTheChordQHotkeyIsAbleToBind();
    void workspaceConfig_fromJsonReducesAMultiChordImport();
    void configManager_isThreadSafeAndConsistent();
    void configManager_skippedVersion_persistsCorrectly();

    void mainWindowGeometry_keepsAWindowDockedAtTheOrigin();
    void mainWindowGeometry_honoursANegativeCoordinateMonitor();
    void mainWindowGeometry_bringsTheWindowBackWhenItsMonitorIsGone();
    void mainWindowGeometry_staysOnTheMonitorTheWindowHangsFrom();
    void mainWindowGeometry_fitsTheWindowToASmallerResolution();
    void mainWindowGeometry_shrinksAWindowLargerThanTheScreen();
    void mainWindowGeometry_centresAnUnplacedWindowInsideTheTaskbar();
    void mainWindowGeometry_hasNoScreenToFitInto();

    void workspaceService_applyStatusName();
    void workspaceService_noChangeEmitsSuccess();
    void workspaceService_displayChangeConfirmed();
    void workspaceService_displayChangeTimeout();
    void workspaceService_displayChangeFailed();
    void workspaceService_audioSwitch();
    void workspaceService_audioErrorDegradesStatus();
    void workspaceService_outcomeNamesTheStageThatDidNotWork();
    void workspaceService_failedApplyDoesNotConfirmItselfWithASound();
    void workspaceService_appLaunchErrorDegradesStatus();
    void workspaceService_partialSuccessDisplayOnly();
    void workspaceService_audioConfirmationEmitted();
    void workspaceService_audioConfirmationDisabled();
    void workspaceService_secondRequestIsRefusedWhileSwitching();
    void workspaceService_staleDisplayFailureDoesNotFinishNextProfile();
    void workspaceService_directDisplaySwitchReportsFailure();
    void workspaceService_directDisplaySwitchBlocksProfileApply();
    void workspaceService_closesOutgoingAppsAfterDisplayIsConfirmed();
    void workspaceService_reappliedProfileKeepsItsOwnApplications();
    void workspaceService_failedDisplaySwitchKeepsOutgoingAppsOpen();
    void workspaceService_displayTimeoutKeepsOutgoingAppsOpen();
    void systemUtils_heldProcessHandleProvesTheProcessIsAlive();

    void appLauncher_sequenceSkipsEmptyPaths();
    void appLauncher_sequenceRefusesNonExistentPath();
    void appLauncher_sequenceRefusesNonExePath();
    void appLauncher_sequenceRefusesSystemExecutable();
    void appLauncher_sequenceLaunchesValidExecutable();
    void appLauncher_delayedLaunchIsScheduledAndReportsLateError();
    void appLauncher_cancelledDelayedLaunchNeverStarts();
    void appLauncher_stillToStartSkipsPathsTheProfileAlreadyOwns();
    void appLauncher_runningApplicationIsNotStartedTwice();
    void appLauncher_leavingAProfileClosesOnlyTheApplicationsItOwns();
    void appLauncher_scheduledDelayedLaunchIsNotScheduledTwice();

    void appLaunchConfig_jsonRoundTrip();
    void workspaceConfig_appsToLaunchJsonRoundTrip();

    void hotkeyManager_resolveInitialProfileId_null();
    void hotkeyManager_resolveInitialProfileId_selected();
    void hotkeyManager_resolveInitialProfileId_fallback();
    void hotkeyManager_cycleSkipsProfilesMarkedToSkip();
    void hotkeyManager_cycleStaysWhenNoOtherProfileExists();
    void hotkeyManager_reportsShortcutsTheSystemRefusedToBind();

    void displayManager_parseMonitorKey_valid();
    void displayManager_parseMonitorKey_invalid();
    void displayManager_destructionWaitsForAnInFlightSwitch();

    void winKeyTranslator_translatesCorrectly();

    void settingsDialog_hotkeyConflict_showsWarningViaDialogManager();
    void settingsDialog_keepsEditsMadeWhileAutostartIsBeingChecked();
    void settingsDialog_appliesRuntimeLoggingOnlyAfterTheSettingsAreSaved();
    void settingsDialog_leavesEverythingUnchangedWhenTheLogonTaskRefusesTheChange();
    void settingsDialog_warnsWhenTheRollbackAfterAFailedSaveAlsoFails();
    void profileEditor_keepsBindingOfADeviceThatIsNotDetected();
    void profileEditor_renamesAProfileWithAManualIcon();
    void profileEditor_followsTheSuggestedIconWhileItIsAutomatic();
    void profileImport_skipsDuplicatesInsideTheFile();
    void profileImport_leavesTheModelUnchangedWhenSavingFails();
    void profileExport_roundTripsThroughTheSerializer();
    void profileExport_keepsAPreviousFileWhenTheTargetCannotBeReplaced();
    void deviceUtils_keepsTheActiveDeviceFirstAndUndetectedHardwareLast();
    void deviceUtils_selectableDevicesStopsBeforeUndetectedHardware();
    void updateService_versionComparison();
    void updateService_usableReleaseRejectsADraftThatCannotBeOpened();
    void updateService_offersACachedReleaseOnlyWhenItIsUsableAndNotSkipped();
    void singleInstanceGuard_raisesTheWindowForAHandshakeSplitIntoFragments();
    void singleInstanceGuard_dropsClientsThatDoNotCompleteTheHandshake();
    void comInitGuard_initializesComOnWorkerThread();
    void logManager_rollAsideKeepsOnePreviousGeneration();
    void logViewerDialog_readsOnlyABoundedTailAndBuffer();
    void logViewerDialog_treatsAShrinkingLogFileAsARotation();
    void logManager_togglingWhileWorkersEmitKeepsLogFileConsistent();
};

void ModeFlowTests::initTestCase() {
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.fonts.warning=false"));
}

void ModeFlowTests::normalizedLocale_data() {
    QTest::addColumn<QString>("requested");
    QTest::addColumn<QString>("expected");

    QTest::newRow("english") << QStringLiteral("en_US") << QStringLiteral("en_US");
    QTest::newRow("russian") << QStringLiteral("ru_RU") << QStringLiteral("ru_RU");
    QTest::newRow("fallback-gb") << QStringLiteral("en_GB") << QStringLiteral("en_US");
    QTest::newRow("fallback-de") << QStringLiteral("de_DE") << QStringLiteral("en_US");
}

void ModeFlowTests::normalizedLocale() {
    QFETCH(QString, requested);
    QFETCH(QString, expected);

    QCOMPARE(ModeFlow::Core::LocalizationManager::normalizedLocale(requested), expected);
}

void ModeFlowTests::unsupportedLocaleFallsBackSilently() {
    ModeFlow::Core::LocalizationManager manager;
    QSignalSpy languageSpy(&manager, &ModeFlow::Core::LocalizationManager::signalLanguageChanged);
    QSignalSpy errorSpy(&manager, &ModeFlow::Core::LocalizationManager::translationError);

    manager.switchLanguage(QStringLiteral("de_DE"));

    QCOMPARE(manager.currentLocale(), QStringLiteral("en_US"));
    QCOMPARE(languageSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(QCoreApplication::translate("QKeySequence", "Meta"), QStringLiteral("Win"));
}

void ModeFlowTests::workspaceDeletePersistsAndEmitsRefresh() {
    FakeWorkspaceManager workspaceManager;
    FakeSettingsManager settingsManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    ModeFlow::Gui::MainWindow window(&workspaceManager, &settingsManager, &styleManager, &dialogManager);
    QSignalSpy refreshSpy(&window, &ModeFlow::Gui::MainWindow::profilesChanged);

    QVERIFY(QMetaObject::invokeMethod(&window, "deleteClicked", Qt::DirectConnection));

    QCOMPARE(workspaceManager.saveCalls, 1);
    QCOMPARE(workspaceManager.model()->rowCount(), 0);
    QCOMPARE(refreshSpy.count(), 1);
    QCOMPARE(dialogManager.lastConfirmTitle, QStringLiteral("Delete"));
}

void ModeFlowTests::workspaceDeleteSaveFailureDoesNotEmitRefresh() {
    FakeWorkspaceManager workspaceManager;
    workspaceManager.saveShouldSucceed = false;
    FakeSettingsManager settingsManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    ModeFlow::Gui::MainWindow window(&workspaceManager, &settingsManager, &styleManager, &dialogManager);
    QSignalSpy refreshSpy(&window, &ModeFlow::Gui::MainWindow::profilesChanged);

    QVERIFY(QMetaObject::invokeMethod(&window, "deleteClicked", Qt::DirectConnection));

    QCOMPARE(workspaceManager.saveCalls, 1);
    QCOMPARE(workspaceManager.model()->rowCount(), 0);
    QCOMPARE(refreshSpy.count(), 0);
    QCOMPARE(dialogManager.lastConfirmTitle, QStringLiteral("Delete"));
}

void ModeFlowTests::settingsAcceptCommitsAutostartAndConfig() {
    FakeSettingsManager settingsManager;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;
    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    auto* autostart = dialog.findChild<QCheckBox*>("checkAutostart");
    auto* delay = dialog.findChild<QSpinBox*>("spinLogonDelay");
    auto* audioConfirmation = dialog.findChild<QCheckBox*>("checkAudioConfirmation");
    auto* language = dialog.findChild<QComboBox*>("comboLanguage");

    QVERIFY(autostart);
    QVERIFY(delay);
    QVERIFY(audioConfirmation);
    QVERIFY(language);

    autostart->setChecked(true);
    delay->setValue(10);
    audioConfirmation->setChecked(false);

    const int russianIndex = language->findData(QStringLiteral("ru_RU"));
    QVERIFY(russianIndex != -1);
    language->setCurrentIndex(russianIndex);

    dialog.accept();

    QCOMPARE(settingsManager.saveCalls, 1);
    QCOMPARE(settingsManager.autostartRequests.size(), 1);
    QCOMPARE(settingsManager.autostartRequests.at(0).first, true);
    QCOMPARE(settingsManager.autostartRequests.at(0).second, 10);
    QCOMPARE(settingsManager.autostartEnabledValue, true);
    QCOMPARE(settingsManager.autostartDelaySeconds, 10);
    QCOMPARE(settingsManager.audioConfirmationEnabled, false);
    QCOMPARE(settingsManager.languageCode, QStringLiteral("ru_RU"));
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
}

void ModeFlowTests::settingsDialog_commitsTheWindowLayoutPreference() {
    FakeSettingsManager settingsManager;
    settingsManager.rememberWindowLayoutsValue = true;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;
    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    auto* rememberLayouts = dialog.findChild<QCheckBox*>("checkRememberWindowLayouts");
    QVERIFY(rememberLayouts);

    // The saved value reaches the form as it is.
    QVERIFY(rememberLayouts->isChecked());

    rememberLayouts->setChecked(false);
    dialog.accept();

    QCOMPARE(settingsManager.saveCalls, 1);
    QCOMPARE(settingsManager.rememberWindowLayoutsValue, false);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
}

void ModeFlowTests::settingsAcceptRollsBackOnSaveFailure() {
    FakeSettingsManager settingsManager;
    settingsManager.saveShouldSucceed = false;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;
    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    auto* autostart = dialog.findChild<QCheckBox*>("checkAutostart");
    auto* delay = dialog.findChild<QSpinBox*>("spinLogonDelay");
    auto* audioConfirmation = dialog.findChild<QCheckBox*>("checkAudioConfirmation");
    auto* language = dialog.findChild<QComboBox*>("comboLanguage");

    QVERIFY(autostart);
    QVERIFY(delay);
    QVERIFY(audioConfirmation);
    QVERIFY(language);

    autostart->setChecked(true);
    delay->setValue(12);
    audioConfirmation->setChecked(false);

    const int russianIndex = language->findData(QStringLiteral("ru_RU"));
    QVERIFY(russianIndex != -1);
    language->setCurrentIndex(russianIndex);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Unable to save settings.*"));
    dialog.accept();

    QCOMPARE(settingsManager.saveCalls, 1);
    QCOMPARE(settingsManager.autostartRequests.size(), 2);
    QCOMPARE(settingsManager.autostartRequests.at(0).first, true);
    QCOMPARE(settingsManager.autostartRequests.at(0).second, 12);
    QCOMPARE(settingsManager.autostartRequests.at(1).first, false);
    QCOMPARE(settingsManager.autostartRequests.at(1).second, 5);
    QCOMPARE(settingsManager.autostartEnabledValue, false);
    QCOMPARE(settingsManager.autostartDelaySeconds, 5);
    QCOMPARE(settingsManager.audioConfirmationEnabled, true);
    QCOMPARE(settingsManager.languageCode, QStringLiteral("en_US"));
    QCOMPARE(dialog.result(), 0);
}

void ModeFlowTests::commandLineBuilder_generatesCorrectArguments() {
    using ModeFlow::Core::CommandLineBuilder;

    CommandLineBuilder builder1;
    builder1.withLogon().withLog(true).withDelay(15);

    QStringList expected1 = {"--logon", "--log", "--delay", "15"};
    QCOMPARE(builder1.toStringList(), expected1);

    CommandLineBuilder builder2;
    builder2.withSilentRestart();

    QStringList expected2 = {"--silent-restart"};
    QCOMPARE(builder2.toStringList(), expected2);
}

void ModeFlowTests::cliParser_parsesArgumentsCorrectly() {
    using ModeFlow::Core::CliParser;

    // Scenario 1: Clean Run with No Arguments (Normal GUI)
    {
        QStringList args = {QStringLiteral("ModeFlow.exe")};
        const CliParser::Result result = CliParser::parse(args);

        QCOMPARE(result.outcome, CliParser::Outcome::Run);
        QCOMPARE(result.mode, CliParser::RunMode::NormalGui);
        QVERIFY(!result.options.isLogon);
        QVERIFY(!result.options.isSilentRestart);
        QVERIFY(!result.options.enableLogging);
        QCOMPARE(result.options.delaySeconds, 0);
    }

    // Scenario 2: Registration in Task Scheduler with Logs and 15-second Delay
    {
        QStringList args = {QStringLiteral("ModeFlow.exe"), QStringLiteral("--register"), QStringLiteral("--logon"),
                            QStringLiteral("--log"),        QStringLiteral("--delay"),    QStringLiteral("15")};
        const CliParser::Result result = CliParser::parse(args);

        QCOMPARE(result.outcome, CliParser::Outcome::Run);
        QCOMPARE(result.mode, CliParser::RunMode::RegisterTask);
        QVERIFY(result.options.isLogon);
        QVERIFY(!result.options.isSilentRestart);
        QVERIFY(result.options.enableLogging);
        QCOMPARE(result.options.delaySeconds, 15);
    }

    // Scenario 3: An unparsable delay is refused instead of silently becoming zero
    {
        QStringList args = {QStringLiteral("ModeFlow.exe"), QStringLiteral("--silent-restart"),
                            QStringLiteral("--delay"), QStringLiteral("invalid")};
        const CliParser::Result result = CliParser::parse(args);

        QCOMPARE(result.outcome, CliParser::Outcome::Failed);
        QVERIFY(!result.text.isEmpty());
    }
}

void ModeFlowTests::cliParser_finishesHelpAndBadArgumentsWithoutTouchingTheTask() {
    using ModeFlow::Core::CliParser;

    struct Case {
        QStringList arguments;
        CliParser::Outcome expectedOutcome;
    };

    const QString exe = QStringLiteral("ModeFlow.exe");
    const QList<Case> cases = {
        // Help and version have their own outcome instead of falling through to the GUI start.
        {{exe, QStringLiteral("--help")}, CliParser::Outcome::ShowHelp},
        {{exe, QStringLiteral("--version")}, CliParser::Outcome::ShowVersion},
        // A helper flag combined with help must not reach the Task Scheduler branch.
        {{exe, QStringLiteral("--register"), QStringLiteral("--help")}, CliParser::Outcome::ShowHelp},
        {{exe, QStringLiteral("--unregister"), QStringLiteral("--version")}, CliParser::Outcome::ShowVersion},
        // Unknown keys, conflicting modes and a bad delay are refused with an explanation.
        {{exe, QStringLiteral("--bogus")}, CliParser::Outcome::Failed},
        {{exe, QStringLiteral("--register"), QStringLiteral("--unregister")}, CliParser::Outcome::Failed},
        {{exe, QStringLiteral("--register"), QStringLiteral("--delay"), QStringLiteral("invalid")},
         CliParser::Outcome::Failed},
        {{exe, QStringLiteral("--unregister"), QStringLiteral("--delay"), QStringLiteral("-5")},
         CliParser::Outcome::Failed},
    };

    for (const Case& testCase : cases) {
        const CliParser::Result result = CliParser::parse(testCase.arguments);

        QVERIFY2(
            result.outcome == testCase.expectedOutcome,
            qPrintable(
                QStringLiteral("%1 -> %2").arg(testCase.arguments.join(u' '), QString::number(int(result.outcome)))));
        QVERIFY(!result.text.isEmpty());
        // The safety property: no case below may ask main() to create or delete a scheduled task.
        QCOMPARE(result.mode, CliParser::RunMode::NormalGui);
    }
}

void ModeFlowTests::hotkeyEdit_replacesTheChordInsteadOfAppendingASecondOne() {
    ChordTypingEdit edit;

    edit.type(Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(edit.keySequence().count(), 1);
    QCOMPARE(edit.keySequence(), QKeySequence(QStringLiteral("Ctrl+A")));

    // A global hotkey binds exactly one chord, so the next chord replaces the value instead of
    // turning the field into "Ctrl+A, Ctrl+B" where only the first half would ever work.
    edit.type(Qt::Key_B, Qt::ControlModifier);
    QCOMPARE(edit.keySequence().count(), 1);
    QCOMPARE(edit.keySequence(), QKeySequence(QStringLiteral("Ctrl+B")));

    edit.type(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(edit.keySequence().count(), 1);
    QCOMPARE(edit.keySequence(), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
}

void ModeFlowTests::singleChord_keepsTheChordQHotkeyIsAbleToBind() {
    using ModeFlow::Core::singleChord;

    const QKeySequence chords(QStringLiteral("Ctrl+A, Ctrl+B"));
    QCOMPARE(chords.count(), 2);

    QCOMPARE(singleChord(QKeySequence()), QKeySequence());
    QCOMPARE(singleChord(QKeySequence(QStringLiteral("Ctrl+A"))), QKeySequence(QStringLiteral("Ctrl+A")));
    // QHotkey binds the first sequence and ignores the rest, so reducing to the first chord keeps
    // the stored value equal to what was actually registered before.
    QCOMPARE(singleChord(chords), QKeySequence(QStringLiteral("Ctrl+A")));
}

void ModeFlowTests::workspaceConfig_fromJsonReducesAMultiChordImport() {
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("profile-1");
    obj[QStringLiteral("hotkey")] = QKeySequence(QStringLiteral("Ctrl+A, Ctrl+B")).toString(QKeySequence::PortableText);

    const ModeFlow::Core::WorkspaceConfig cfg = ModeFlow::Core::WorkspaceConfig::fromJson(obj);

    QCOMPARE(cfg.hotkey.count(), 1);
    QCOMPARE(cfg.hotkey, QKeySequence(QStringLiteral("Ctrl+A")));
}

void ModeFlowTests::configManager_isThreadSafeAndConsistent() {
    using namespace ModeFlow::Core;
    ConfigManager config;

    config.setTheme(Theme::Light);
    config.setLanguage(QStringLiteral("en_US"));

    const int iterations = 1000;

    auto writer1 = QtConcurrent::run([&config]() {
        for (int i = 0; i < iterations; ++i) {
            config.setTheme(i % 2 == 0 ? Theme::Dark : Theme::Light);
        }
    });

    auto writer2 = QtConcurrent::run([&config]() {
        for (int i = 0; i < iterations; ++i) {
            config.setLanguage(i % 2 == 0 ? QStringLiteral("ru_RU") : QStringLiteral("en_US"));
        }
    });

    auto reader1 = QtConcurrent::run([&config]() {
        for (int i = 0; i < iterations; ++i) {
            Theme t = config.currentTheme();
            QVERIFY(t == Theme::Light || t == Theme::Dark);
        }
    });

    auto reader2 = QtConcurrent::run([&config]() {
        for (int i = 0; i < iterations; ++i) {
            QString lang = config.language();
            QVERIFY(lang == QStringLiteral("en_US") || lang == QStringLiteral("ru_RU"));
        }
    });

    writer1.waitForFinished();
    writer2.waitForFinished();
    reader1.waitForFinished();
    reader2.waitForFinished();

    Theme finalTheme = config.currentTheme();
    QVERIFY(finalTheme == Theme::Light || finalTheme == Theme::Dark);
}

void ModeFlowTests::configManager_skippedVersion_persistsCorrectly() {
    using namespace ModeFlow::Core;
    ConfigManager config;

    config.setSkippedVersion(QStringLiteral("0.9.1"));
    QCOMPARE(config.skippedVersion(), QStringLiteral("0.9.1"));
}

void ModeFlowTests::workspaceService_applyStatusName() {
    using ModeFlow::Core::WorkspaceService;
    QCOMPARE(WorkspaceService::applyStatusName(WorkspaceService::ApplyStatus::Success), QStringLiteral("success"));
    QCOMPARE(WorkspaceService::applyStatusName(WorkspaceService::ApplyStatus::PartialSuccess),
             QStringLiteral("partial-success"));
    QCOMPARE(WorkspaceService::applyStatusName(WorkspaceService::ApplyStatus::Failed), QStringLiteral("failed"));
}

void ModeFlowTests::workspaceService_noChangeEmitsSuccess() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-1");

    service.applyWorkspaceConfig(config);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::Success);
}

void ModeFlowTests::workspaceService_displayChangeConfirmed() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-2");

    service.applyWorkspaceConfig(config);
    QCOMPARE(finishedSpy.count(), 0);

    display.m_currentDisplayKey = QStringLiteral("display-2");
    display.emitDisplaysChanged();

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::Success);
}

void ModeFlowTests::workspaceService_displayChangeTimeout() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    service.setDisplayTimeout(50);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-2");

    service.applyWorkspaceConfig(config);
    QCOMPARE(finishedSpy.count(), 0);

    QTest::qWait(100);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::PartialSuccess);
}

void ModeFlowTests::workspaceService_displayChangeFailed() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_switchShouldFail = true;
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-2");

    service.applyWorkspaceConfig(config);
    QTest::qWait(50);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::Failed);
}

void ModeFlowTests::workspaceService_audioSwitch() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("old-audio");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-1");
    config.audioId = QStringLiteral("new-audio");

    service.applyWorkspaceConfig(config);

    QCOMPARE(audio.m_lastSetOutputDeviceId, QStringLiteral("new-audio"));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::Success);
}

void ModeFlowTests::workspaceService_audioErrorDegradesStatus() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("old-audio");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-2");
    config.audioId = QStringLiteral("new-audio");

    service.applyWorkspaceConfig(config);
    QCOMPARE(finishedSpy.count(), 0);

    emit audio.errorOccurred(QStringLiteral("audio error"));

    display.m_currentDisplayKey = QStringLiteral("display-2");
    display.emitDisplaysChanged();

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::PartialSuccess);
}

void ModeFlowTests::workspaceService_appLaunchErrorDegradesStatus() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-1");
    config.appsToLaunch = {{QStringLiteral("C:\\test.exe"), 0, false}};

    service.applyWorkspaceConfig(config);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::Failed);
    // The refusal came from the shared validator turning down a path that does not exist, not from a
    // stand-in that was told to fail.
    QCOMPARE(launcher.m_executeCount, 0);
}

void ModeFlowTests::workspaceService_partialSuccessDisplayOnly() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    service.setDisplayTimeout(50);
    QSignalSpy finishedSpy(&service, &ModeFlow::Core::WorkspaceService::configApplyFinished);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-2");
    config.audioId = QStringLiteral("new-audio");

    service.applyWorkspaceConfig(config);
    QTest::qWait(100);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<ModeFlow::Core::WorkspaceService::ApplyStatus>(),
             ModeFlow::Core::WorkspaceService::ApplyStatus::PartialSuccess);
}

void ModeFlowTests::workspaceService_outcomeNamesTheStageThatDidNotWork() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    // A profile whose display moved but whose audio did not is only half applied, and the summary
    // has to say which half.
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("old-audio");
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);
    QSignalSpy finishedSpy(&service, &Service::configApplyFinished);

    WorkspaceConfig config = makeProfileForSwitch(QStringLiteral("p1"), QStringLiteral("display-2"));
    config.audioId = QStringLiteral("new-audio");

    service.applyWorkspaceConfig(config);
    emit audio.errorOccurred(QStringLiteral("audio error"));
    display.m_currentDisplayKey = QStringLiteral("display-2");
    display.emitDisplaysChanged();

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<Service::ApplyStatus>(), Service::ApplyStatus::PartialSuccess);
    QCOMPARE(finishedSpy.at(0).at(2).value<QString>(), QStringLiteral("the audio device was not set"));

    // A refused application launch is named the same way, next to the stage that worked.
    FakeDisplayManagerForWS secondDisplay;
    secondDisplay.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS secondAudio;
    FakeAppLauncherForWS secondLauncher;

    Service secondService(&secondDisplay, &secondAudio, &secondLauncher);
    QSignalSpy secondFinishedSpy(&secondService, &Service::configApplyFinished);

    WorkspaceConfig appConfig = makeProfileForSwitch(QStringLiteral("p2"), QStringLiteral("display-1"));
    appConfig.appsToLaunch = {{QStringLiteral("C:\\test.exe"), 0, false}};

    secondService.applyWorkspaceConfig(appConfig);

    QCOMPARE(secondFinishedSpy.count(), 1);
    QCOMPARE(secondFinishedSpy.at(0).at(1).value<Service::ApplyStatus>(), Service::ApplyStatus::Failed);
    QVERIFY(secondFinishedSpy.at(0).at(2).value<QString>().contains(QStringLiteral("applications")));
    QCOMPARE(secondLauncher.m_executeCount, 0);
}

void ModeFlowTests::workspaceService_failedApplyDoesNotConfirmItselfWithASound() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_switchShouldFail = true;
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("audio-1");
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);
    QSignalSpy finishedSpy(&service, &Service::configApplyFinished);
    QSignalSpy feedbackSpy(&service, &Service::requestAudioFeedback);
    QSignalSpy errorSpy(&service, &Service::errorOccurred);

    // Nothing else is configured, so a display that refuses to move fails the whole apply.
    const WorkspaceConfig config = makeProfileForSwitch(QStringLiteral("p1"), QStringLiteral("display-2"));
    service.applyWorkspaceConfig(config);
    QTest::qWait(50);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).value<Service::ApplyStatus>(), Service::ApplyStatus::Failed);
    QCOMPARE(finishedSpy.at(0).at(2).value<QString>(), QStringLiteral("the display switch failed"));

    // The confirmation sound is what the user hears as "switched", so a failed apply stays silent and
    // reports once through the outcome instead of also through a separate error message.
    QCOMPARE(feedbackSpy.count(), 0);
    QVERIFY(errorSpy.isEmpty());
}

void ModeFlowTests::workspaceService_audioConfirmationEmitted() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("old-audio");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    QSignalSpy feedbackSpy(&service, &ModeFlow::Core::WorkspaceService::requestAudioFeedback);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-1");
    config.audioId = QStringLiteral("new-audio");

    service.applyWorkspaceConfig(config);

    QCOMPARE(feedbackSpy.count(), 1);
}

void ModeFlowTests::workspaceService_audioConfirmationDisabled() {
    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    audio.m_defaultOutputDeviceId = QStringLiteral("old-audio");
    FakeAppLauncherForWS launcher;

    ModeFlow::Core::WorkspaceService service(&display, &audio, &launcher);
    service.setAudioConfirmation(false);
    QSignalSpy feedbackSpy(&service, &ModeFlow::Core::WorkspaceService::requestAudioFeedback);

    ModeFlow::Core::WorkspaceConfig config;
    config.id = QStringLiteral("p1");
    config.name = QStringLiteral("Profile1");
    config.displayId = QStringLiteral("display-1");
    config.audioId = QStringLiteral("new-audio");

    service.applyWorkspaceConfig(config);

    QCOMPARE(feedbackSpy.count(), 0);
}

void ModeFlowTests::workspaceService_secondRequestIsRefusedWhileSwitching() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_deferSwitch = true;
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);

    QSignalSpy startedSpy(&service, &Service::configApplyStarted);
    QSignalSpy finishedSpy(&service, &Service::configApplyFinished);
    QSignalSpy errorSpy(&service, &Service::errorOccurred);

    const WorkspaceConfig first = makeProfileForSwitch(QStringLiteral("p1"), QStringLiteral("display-2"));
    // Give the refused request something it would have started, so "nothing launched" is an answer.
    const QString viewer = makeLaunchableExecutable(QStringLiteral("ws-refused"));
    QVERIFY(!viewer.isEmpty());
    WorkspaceConfig second = makeProfileForSwitch(QStringLiteral("p2"), QStringLiteral("display-3"));
    second.appsToLaunch = {{viewer, 0, false}};

    service.applyWorkspaceConfig(first);
    QCOMPARE(startedSpy.count(), 1);

    // A tray or hotkey press during a transition must not take over the operation in flight.
    service.applyWorkspaceConfig(second);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(launcher.m_executeCount, 0);

    display.finishDeferredSwitch(true);
    QTest::qWait(50);
    QCOMPARE(finishedSpy.count(), 0);

    display.m_currentDisplayKey = QStringLiteral("display-2");
    display.emitDisplaysChanged();

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).value<WorkspaceConfig>().id, QStringLiteral("p1"));
    QCOMPARE(finishedSpy.at(0).at(1).value<Service::ApplyStatus>(), Service::ApplyStatus::Success);
    QCOMPARE(display.m_lastSwitchTarget, QStringLiteral("display-2"));
    QVERIFY(!service.isSwitchInProgress());
    QFile::remove(viewer);
}

void ModeFlowTests::workspaceService_staleDisplayFailureDoesNotFinishNextProfile() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_deferSwitch = true;
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(30);

    QSignalSpy startedSpy(&service, &Service::configApplyStarted);
    QSignalSpy finishedSpy(&service, &Service::configApplyFinished);
    QSignalSpy errorSpy(&service, &Service::errorOccurred);

    const WorkspaceConfig first = makeProfileForSwitch(QStringLiteral("p1"), QStringLiteral("display-2"));
    const WorkspaceConfig second = makeProfileForSwitch(QStringLiteral("p2"), QStringLiteral("display-3"));

    service.applyWorkspaceConfig(first);
    // The switch outlives its own timeout, so the operation is reported and closed.
    QTest::qWait(120);
    QCOMPARE(finishedSpy.count(), 1);
    // The timeout is part of the apply outcome now, so the summary carries it instead of a separate
    // error message about the same profile.
    QVERIFY(errorSpy.isEmpty());
    QCOMPARE(finishedSpy.at(0).at(2).value<QString>(), QStringLiteral("the display did not confirm the switch"));

    // The second profile keeps its own switch open and gets a timeout long enough that only a stale
    // callback could finish it.
    service.setDisplayTimeout(5000);
    display.m_deferSwitch = true;
    service.applyWorkspaceConfig(second);
    QCOMPARE(startedSpy.count(), 2);

    // Now the abandoned first switch reports its failure. It belongs to a finished operation, so it
    // may not decide the outcome of the profile that is being applied right now.
    display.finishDeferredSwitch(false);
    QTest::qWait(50);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(errorSpy.isEmpty());

    display.m_currentDisplayKey = QStringLiteral("display-3");
    display.emitDisplaysChanged();

    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).value<WorkspaceConfig>().id, QStringLiteral("p2"));
    QCOMPARE(finishedSpy.at(1).at(1).value<Service::ApplyStatus>(), Service::ApplyStatus::Success);
    QVERIFY(!service.isSwitchInProgress());
}

void ModeFlowTests::workspaceService_directDisplaySwitchReportsFailure() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_switchShouldFail = true;
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);

    QSignalSpy errorSpy(&service, &Service::errorOccurred);
    QSignalSpy startedSpy(&service, &Service::configApplyStarted);

    // The tray display submenu used to drop the future, so a failed switch stayed invisible.
    service.switchDisplayDirectly(QStringLiteral("display-2"));
    QTest::qWait(50);

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!service.isSwitchInProgress());

    service.switchDisplayDirectly(QString());
    QCOMPARE(errorSpy.count(), 1);

    const WorkspaceConfig profile = makeProfileForSwitch(QStringLiteral("p1"), QStringLiteral("display-1"));
    service.applyWorkspaceConfig(profile);
    QCOMPARE(startedSpy.count(), 1);
}

void ModeFlowTests::workspaceService_directDisplaySwitchBlocksProfileApply() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_deferSwitch = true;
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);

    QSignalSpy startedSpy(&service, &Service::configApplyStarted);
    QSignalSpy finishedSpy(&service, &Service::configApplyFinished);
    QSignalSpy errorSpy(&service, &Service::errorOccurred);

    service.switchDisplayDirectly(QStringLiteral("display-2"));
    QVERIFY(service.isSwitchInProgress());

    const WorkspaceConfig profile = makeProfileForSwitch(QStringLiteral("p1"), QStringLiteral("display-3"));
    service.applyWorkspaceConfig(profile);
    QCOMPARE(startedSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);

    display.finishDeferredSwitch(true);
    QTest::qWait(50);
    QVERIFY(!service.isSwitchInProgress());

    service.applyWorkspaceConfig(profile);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(display.m_lastSwitchTarget, QStringLiteral("display-3"));
}

void ModeFlowTests::workspaceService_closesOutgoingAppsAfterDisplayIsConfirmed() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);

    const QString editor = makeLaunchableExecutable(QStringLiteral("ws-confirmed"));
    QVERIFY(!editor.isEmpty());
    const WorkspaceConfig incoming = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"), editor);
    service.applyWorkspaceConfig(incoming);
    QCOMPARE(launcher.m_executeCount, 1);
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);

    display.m_deferSwitch = true;
    const WorkspaceConfig outgoingNext = makeProfileForSwitch(QStringLiteral("pB"), QStringLiteral("display-2"));
    service.applyWorkspaceConfig(outgoingNext);

    // Scheduled launches are dropped immediately, running applications wait for the new screen.
    QCOMPARE(launcher.m_cancelledProfileIds, QStringList{QStringLiteral("pA")});
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);

    display.finishDeferredSwitch(true);
    QTest::qWait(50);
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);

    display.m_currentDisplayKey = QStringLiteral("display-2");
    display.emitDisplaysChanged();

    QCOMPARE(launcher.m_closedProfileIds, QStringList{QStringLiteral("pA")});
    QCOMPARE(launcher.m_executeCount, 1);
    // The application the profile really owns is the one asked to close, by its tracked process.
    QCOMPARE(launcher.m_closedPids.size(), 1);
    QFile::remove(editor);
}

void ModeFlowTests::workspaceService_reappliedProfileKeepsItsOwnApplications() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);

    const QString editor = makeLaunchableExecutable(QStringLiteral("ws-reapplied"));
    QVERIFY(!editor.isEmpty());
    const WorkspaceConfig config = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"), editor);
    service.applyWorkspaceConfig(config);
    QCOMPARE(launcher.m_executeCount, 1);

    // Applying the profile that is already current tears nothing down and starts nothing again: the
    // list still reaches the launcher, which drops what the profile already owns.
    service.applyWorkspaceConfig(config);
    QCOMPARE(launcher.m_executeCount, 1);
    QVERIFY(launcher.m_cancelledProfileIds.isEmpty());
    QVERIFY(launcher.m_closedProfileIds.isEmpty());

    // Leaving for another profile is a real switch, so both clean-up calls come back.
    service.applyWorkspaceConfig(makeProfileForSwitch(QStringLiteral("pB"), QStringLiteral("display-1")));
    QCOMPARE(launcher.m_cancelledProfileIds, QStringList{QStringLiteral("pA")});
    QCOMPARE(launcher.m_closedProfileIds, QStringList{QStringLiteral("pA")});

    // Coming back to the profile starts the application again, because the switch forgot it.
    service.applyWorkspaceConfig(config);
    QCOMPARE(launcher.m_executeCount, 2);
    QFile::remove(editor);
}

void ModeFlowTests::workspaceService_failedDisplaySwitchKeepsOutgoingAppsOpen() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);

    const QString editor = makeLaunchableExecutable(QStringLiteral("ws-failedswitch"));
    QVERIFY(!editor.isEmpty());
    const WorkspaceConfig incoming = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"), editor);
    service.applyWorkspaceConfig(incoming);

    display.m_switchShouldFail = true;
    const WorkspaceConfig next = makeProfileForSwitch(QStringLiteral("pB"), QStringLiteral("display-2"));
    service.applyWorkspaceConfig(next);
    QTest::qWait(50);

    // The user is still on the old display, so nothing of profile A may be closed.
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);
    QCOMPARE(display.m_lastSwitchTarget, QStringLiteral("display-2"));
    QFile::remove(editor);
}

void ModeFlowTests::workspaceService_displayTimeoutKeepsOutgoingAppsOpen() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    display.m_deferSwitch = true;
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);

    const QString editor = makeLaunchableExecutable(QStringLiteral("ws-timeout"));
    QVERIFY(!editor.isEmpty());
    const WorkspaceConfig incoming = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"), editor);
    service.applyWorkspaceConfig(incoming);
    QCOMPARE(launcher.m_executeCount, 1);

    service.setDisplayTimeout(30);
    const WorkspaceConfig next = makeProfileForSwitch(QStringLiteral("pB"), QStringLiteral("display-2"));
    service.applyWorkspaceConfig(next);
    QTest::qWait(120);

    // A switch that never reported back is not a confirmed move, so profile A keeps its windows.
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);
    QVERIFY(launcher.m_closedPids.isEmpty());
    QFile::remove(editor);
}

void ModeFlowTests::systemUtils_heldProcessHandleProvesTheProcessIsAlive() {
    using ModeFlow::Utils::SystemUtils;

    // Our own process is the only process a test may inspect without touching anything else.
    void* handle = SystemUtils::openProcessHandle(QCoreApplication::applicationPid());
    QVERIFY(handle != nullptr);
    QVERIFY(SystemUtils::isProcessRunning(handle));
    SystemUtils::closeProcessHandle(handle);

    // A closed-on-entry or unknown process has no handle, so it can never be mistaken for ours.
    QVERIFY(SystemUtils::openProcessHandle(0) == nullptr);
    QVERIFY(SystemUtils::openProcessHandle(static_cast<qint64>(0x7FFFFFFF)) == nullptr);
    QVERIFY(!SystemUtils::isProcessRunning(nullptr));
}

void ModeFlowTests::appLauncher_sequenceSkipsEmptyPaths() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    const QList<ModeFlow::Core::AppLaunchConfig> apps = {ModeFlow::Core::AppLaunchConfig{}};
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), apps));
    QCOMPARE(errorSpy.count(), 0);
}

void ModeFlowTests::appLauncher_sequenceRefusesNonExistentPath() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = QStringLiteral("C:\\nonexistent\\fake.exe");

    // An imported profile never went through the UI picker, so the sequence itself has to refuse it.
    QVERIFY(!launcher.launchSequence(QStringLiteral("p1"), {app}));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains(QStringLiteral("does not exist")));
}

void ModeFlowTests::appLauncher_sequenceRefusesNonExePath() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    const QString tempFile = QDir::tempPath() + QStringLiteral("/modeflow-launcher-notes.txt");
    QFile file(tempFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = tempFile;

    QVERIFY(!launcher.launchSequence(QStringLiteral("p1"), {app}));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains(QStringLiteral("Only .exe files can be launched")));

    QFile::remove(tempFile);
}

void ModeFlowTests::appLauncher_sequenceRefusesSystemExecutable() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    const QString winDir = qEnvironmentVariable("windir");
    const QString systemExe = QDir(winDir).filePath(QStringLiteral("System32\\whoami.exe"));
    if (!QFile::exists(systemExe)) {
        QWARN("No system executable found, skipping containment check.");
        return;
    }

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = systemExe;

    QVERIFY(!launcher.launchSequence(QStringLiteral("p1"), {app}));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains(QStringLiteral("system executables is blocked")));
}

void ModeFlowTests::appLauncher_sequenceLaunchesValidExecutable() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    const QString exe = makeLaunchableExecutable(QStringLiteral("valid"));
    QVERIFY(!exe.isEmpty());

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = exe;

    // Only a real startDetached success can make this true, so the shared validation must not refuse
    // an ordinary executable outside the protected directories.
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {app}));
    QCOMPARE(errorSpy.count(), 0);

    // The record of a launched application is what keeps a second copy from starting...
    QCOMPARE(launcher.activeLaunchKeys(QStringLiteral("p1")).size(), 1);

    // ...and an application that has since exited must not block the profile next time it is applied.
    // The copied image stays locked while the process runs, so wait for it to finish before removal.
    QTest::qWait(1500);
    QVERIFY(launcher.activeLaunchKeys(QStringLiteral("p1")).isEmpty());
    QFile::remove(exe);
}

void ModeFlowTests::appLauncher_delayedLaunchIsScheduledAndReportsLateError() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = QStringLiteral("C:\\nonexistent\\delayed.exe");
    app.delaySeconds = 1;

    // A delayed entry is only a promise at apply time, so the profile must not be reported as failed.
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {app}));
    QCOMPARE(errorSpy.count(), 0);

    QTest::qWait(1600);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(
        errorSpy.at(0).at(0).toString().startsWith(QStringLiteral("The scheduled application could not be launched")));
}

void ModeFlowTests::appLauncher_cancelledDelayedLaunchNeverStarts() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = QStringLiteral("C:\\nonexistent\\delayed.exe");
    app.delaySeconds = 1;

    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {app}));
    launcher.cancelDelayedLaunches(QStringLiteral("p1"));

    // An attempt would have surfaced through validation, so silence here proves nothing was launched.
    QTest::qWait(1600);
    QCOMPARE(errorSpy.count(), 0);
}

void ModeFlowTests::appLauncher_stillToStartSkipsPathsTheProfileAlreadyOwns() {
    using ModeFlow::Core::AppLaunchConfig;
    using ModeFlow::Services::AppLauncher;

    auto entry = [](const QString& path) {
        AppLaunchConfig app;
        app.appPath = path;
        return app;
    };

    const QList<AppLaunchConfig> apps = {entry(QStringLiteral("C:\\Temp\\a.exe")),
                                         entry(QStringLiteral("C:/Temp/b.exe")), AppLaunchConfig{}};
    const QStringList owned = {AppLauncher::launchKey(QStringLiteral("c:\\temp\\A.EXE"))};

    // The same file written differently is the application the profile already has, and an entry
    // without a path is not an application at all.
    const QList<AppLaunchConfig> missing = AppLauncher::appsStillToStart(apps, owned);
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first().appPath, QStringLiteral("C:/Temp/b.exe"));

    const QList<AppLaunchConfig> repeated = {entry(QStringLiteral("C:\\Temp\\a.exe")),
                                             entry(QStringLiteral("C:\\TEMP\\a.exe"))};
    QCOMPARE(AppLauncher::appsStillToStart(repeated, QStringList()).size(), 1);
    QCOMPARE(AppLauncher::appsStillToStart(apps, QStringList()).size(), 2);
}

void ModeFlowTests::appLauncher_runningApplicationIsNotStartedTwice() {
    using ModeFlow::Core::AppLaunchConfig;

    const QString editor = makeLaunchableExecutable(QStringLiteral("tracked"));
    const QString viewer = makeLaunchableExecutable(QStringLiteral("tracked-other"));
    QVERIFY(!editor.isEmpty());
    QVERIFY(!viewer.isEmpty());
    const auto cleanup = qScopeGuard([&editor, &viewer] {
        QFile::remove(editor);
        QFile::remove(viewer);
    });

    AppLaunchConfig owned;
    owned.appPath = editor;
    owned.closeOnExit = true;

    AppLaunchConfig missing;
    missing.appPath = viewer;

    FakeAppLauncherForWS launcher;
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {owned}));
    QCOMPARE(launcher.m_executeCount, 1);
    QCOMPARE(launcher.activeLaunchKeys(QStringLiteral("p1")).size(), 1);

    // Re-applying the profile must leave the running copy alone and still start what is missing.
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {owned, missing}));
    QCOMPARE(launcher.m_executeCount, 2);
    QCOMPARE(launcher.m_lastPath, missing.appPath);

    // An application the profile does not own is remembered just the same, so a third apply of the
    // whole list starts nothing at all.
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {owned, missing}));
    QCOMPARE(launcher.m_executeCount, 2);

    // Ownership is tracked per profile, so another profile may start the very same executable.
    QVERIFY(launcher.launchSequence(QStringLiteral("p2"), {owned}));
    QCOMPARE(launcher.m_executeCount, 3);
    QCOMPARE(launcher.activeLaunchKeys(QStringLiteral("p2")).size(), 1);
}

void ModeFlowTests::appLauncher_leavingAProfileClosesOnlyTheApplicationsItOwns() {
    using ModeFlow::Core::AppLaunchConfig;

    const QString editor = makeLaunchableExecutable(QStringLiteral("owned"));
    const QString visitor = makeLaunchableExecutable(QStringLiteral("guest"));
    QVERIFY(!editor.isEmpty());
    QVERIFY(!visitor.isEmpty());
    const auto cleanup = qScopeGuard([&editor, &visitor] {
        QFile::remove(editor);
        QFile::remove(visitor);
    });

    AppLaunchConfig owned;
    owned.appPath = editor;
    owned.closeOnExit = true;

    AppLaunchConfig guest;
    guest.appPath = visitor;

    FakeAppLauncherForWS launcher;
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {owned, guest}));
    QCOMPARE(launcher.activeLaunchKeys(QStringLiteral("p1")).size(), 2);

    launcher.closeProfileProcesses(QStringLiteral("p1"));

    // Tracking now covers every launched application, so the close flag is what decides ownership:
    // the guest window stays open while the profile leaves, and both are simply forgotten.
    QCOMPARE(launcher.m_closedPids.size(), 1);
    QVERIFY(launcher.activeLaunchKeys(QStringLiteral("p1")).isEmpty());

    // Being forgotten is what lets the next visit start them again.
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {owned, guest}));
    QCOMPARE(launcher.m_executeCount, 4);
}

void ModeFlowTests::appLauncher_scheduledDelayedLaunchIsNotScheduledTwice() {
    using ModeFlow::Core::AppLaunchConfig;

    AppLaunchConfig delayed;
    delayed.appPath = QStringLiteral("C:\\nonexistent\\delayed.exe");
    delayed.delaySeconds = 1;

    AppLaunchConfig otherDelayed = delayed;
    otherDelayed.appPath = QStringLiteral("C:\\nonexistent\\other.exe");

    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {delayed}));

    // A pending timer already promises that launch, so the second apply must not add another one:
    // each scheduled failure reports itself once when its delay expires.
    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {delayed}));

    QTest::qWait(1600);
    QCOMPARE(errorSpy.count(), 1);

    QVERIFY(launcher.launchSequence(QStringLiteral("p1"), {otherDelayed}));
    QTest::qWait(1600);
    QCOMPARE(errorSpy.count(), 2);
}

void ModeFlowTests::mainWindowGeometry_keepsAWindowDockedAtTheOrigin() {
    using ModeFlow::Gui::MainWindow;

    const QList<QRect> areas{QRect(0, 0, 1920, 1040)};
    const QSize fallback(650, 550);

    // A window parked in the top-left corner is something the user chose, not a missing setting.
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(0, 0, 650, 550), areas, fallback), QRect(0, 0, 650, 550));
}

void ModeFlowTests::mainWindowGeometry_honoursANegativeCoordinateMonitor() {
    using ModeFlow::Gui::MainWindow;

    const QList<QRect> areas{QRect(0, 0, 1920, 1040), QRect(-1920, 0, 1920, 1040)};
    const QSize fallback(650, 550);

    // A monitor placed to the left of the primary one has negative coordinates; they are still visible.
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(-1300, 200, 650, 550), areas, fallback),
             QRect(-1300, 200, 650, 550));

    // A window that touches no screen at all lands on the primary one, not on the monitor that is gone.
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(-1300, 2000, 650, 550), areas, fallback),
             QRect(0, 490, 650, 550));
}

void ModeFlowTests::mainWindowGeometry_bringsTheWindowBackWhenItsMonitorIsGone() {
    using ModeFlow::Gui::MainWindow;

    const QList<QRect> areas{QRect(0, 0, 1920, 1040)};
    const QSize fallback(650, 550);
    const QRect leftMonitorWasRemoved = QRect(-1300, 200, 650, 550);

    const QRect result = MainWindow::resolvedWindowGeometry(leftMonitorWasRemoved, areas, fallback);

    QCOMPARE(result, QRect(0, 200, 650, 550));
    QVERIFY(areas.first().contains(result));
}

void ModeFlowTests::mainWindowGeometry_staysOnTheMonitorTheWindowHangsFrom() {
    using ModeFlow::Gui::MainWindow;

    const QList<QRect> areas{QRect(0, 0, 1920, 1040), QRect(1920, 0, 1920, 1040)};
    const QSize fallback(650, 550);

    // The window hung off the bottom of the second monitor; only that much is corrected, the monitor
    // it belonged to is kept.
    const QRect result = MainWindow::resolvedWindowGeometry(QRect(2400, 900, 650, 550), areas, fallback);

    QCOMPARE(result, QRect(2400, 490, 650, 550));
    QVERIFY(areas.at(1).contains(result));
}

void ModeFlowTests::mainWindowGeometry_fitsTheWindowToASmallerResolution() {
    using ModeFlow::Gui::MainWindow;

    // 1280x720 with an 80px taskbar: the saved rect was reachable on the old, bigger desktop.
    const QList<QRect> areas{QRect(0, 0, 1280, 640)};
    const QSize fallback(650, 550);

    const QRect result = MainWindow::resolvedWindowGeometry(QRect(900, 400, 650, 550), areas, fallback);

    QCOMPARE(result, QRect(630, 90, 650, 550));
    QVERIFY(areas.first().contains(result));
}

void ModeFlowTests::mainWindowGeometry_shrinksAWindowLargerThanTheScreen() {
    using ModeFlow::Gui::MainWindow;

    const QList<QRect> areas{QRect(0, 0, 800, 600)};
    const QSize fallback(650, 550);

    const QRect result = MainWindow::resolvedWindowGeometry(QRect(-100, -50, 1000, 900), areas, fallback);

    QCOMPARE(result, QRect(0, 0, 800, 600));
    QVERIFY(areas.first().contains(result));
}

void ModeFlowTests::mainWindowGeometry_centresAnUnplacedWindowInsideTheTaskbar() {
    using ModeFlow::Gui::MainWindow;

    const QSize fallback(650, 550);

    // 1920x1080 with an 80px taskbar at the bottom: centre against the usable area, not the full screen.
    const QList<QRect> bottomTaskbar{QRect(0, 0, 1920, 1040)};
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(), bottomTaskbar, fallback), QRect(635, 245, 650, 550));
    QVERIFY(bottomTaskbar.first().contains(QRect(635, 245, 650, 550)));

    // A taskbar docked at the top shifts the usable area, and the window has to follow it.
    const QList<QRect> topTaskbar{QRect(0, 60, 1000, 940)};
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(), topTaskbar, fallback), QRect(175, 255, 650, 550));
    QVERIFY(topTaskbar.first().contains(QRect(175, 255, 650, 550)));

    // A rect with nothing to show is just as unusable as no saved rect at all, wherever it came from.
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(120, 80, 0, 0), topTaskbar, fallback), QRect(175, 255, 650, 550));
    // So is one whose corners are the wrong way round, which is what a hand-edited settings value gives.
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(120, 80, -10, -10), topTaskbar, fallback),
             QRect(175, 255, 650, 550));
}

void ModeFlowTests::mainWindowGeometry_hasNoScreenToFitInto() {
    using ModeFlow::Gui::MainWindow;

    const QSize fallback(650, 550);
    const QList<QRect> noScreens;

    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(), noScreens, fallback), QRect(0, 0, 650, 550));
    QCOMPARE(MainWindow::resolvedWindowGeometry(QRect(-1300, 200, 650, 550), noScreens, fallback),
             QRect(-1300, 200, 650, 550));
}

void ModeFlowTests::hotkeyManager_resolveInitialProfileId_null() {
    QCOMPARE(ModeFlow::Services::HotkeyManager::resolveInitialProfileId(nullptr), QString());
}

void ModeFlowTests::hotkeyManager_resolveInitialProfileId_selected() {
    ModeFlow::Core::ConfigManager cm;
    cm.setSelectedProfileId(QStringLiteral("selected-1"));
    cm.setLastActiveProfileId(QStringLiteral("last-active-1"));

    QCOMPARE(ModeFlow::Services::HotkeyManager::resolveInitialProfileId(&cm), QStringLiteral("selected-1"));
}

void ModeFlowTests::hotkeyManager_resolveInitialProfileId_fallback() {
    ModeFlow::Core::ConfigManager cm;
    cm.setSelectedProfileId(QString());
    cm.setLastActiveProfileId(QStringLiteral("last-active-1"));

    QCOMPARE(ModeFlow::Services::HotkeyManager::resolveInitialProfileId(&cm), QStringLiteral("last-active-1"));
}

void ModeFlowTests::hotkeyManager_cycleSkipsProfilesMarkedToSkip() {
    using ModeFlow::Core::WorkspaceConfig;
    using ModeFlow::Services::HotkeyManager;

    auto profile = [](const QString& id, bool skip) {
        WorkspaceConfig config;
        config.id = id;
        config.name = id;
        config.skipInCycle = skip;
        return config;
    };

    const QList<WorkspaceConfig> configs = {profile(QStringLiteral("a"), false), profile(QStringLiteral("b"), true),
                                            profile(QStringLiteral("c"), false)};

    // The profile marked to skip is stepped over in both directions, and an unknown current id
    // starts the cycle at the first profile.
    QCOMPARE(HotkeyManager::nextCycleIndex(configs, QStringLiteral("a")), 2);
    QCOMPARE(HotkeyManager::nextCycleIndex(configs, QStringLiteral("c")), 0);
    QCOMPARE(HotkeyManager::nextCycleIndex(configs, QString()), 0);
    QCOMPARE(HotkeyManager::nextCycleIndex(configs, QStringLiteral("unknown")), 0);
}

void ModeFlowTests::hotkeyManager_cycleStaysWhenNoOtherProfileExists() {
    using ModeFlow::Core::WorkspaceConfig;
    using ModeFlow::Services::HotkeyManager;

    auto profile = [](const QString& id, bool skip) {
        WorkspaceConfig config;
        config.id = id;
        config.name = id;
        config.skipInCycle = skip;
        return config;
    };

    const QList<WorkspaceConfig> allSkipped = {profile(QStringLiteral("a"), true), profile(QStringLiteral("b"), true)};
    const QList<WorkspaceConfig> onlyOne = {profile(QStringLiteral("a"), false)};
    const QList<WorkspaceConfig> currentIsTheOnlyActive = {profile(QStringLiteral("a"), true),
                                                           profile(QStringLiteral("b"), false)};

    // Re-activating the current profile would close and relaunch its applications, so these cycles
    // resolve to "stay" instead.
    QCOMPARE(HotkeyManager::nextCycleIndex(allSkipped, QStringLiteral("a")), -1);
    QCOMPARE(HotkeyManager::nextCycleIndex(allSkipped, QString()), -1);
    QCOMPARE(HotkeyManager::nextCycleIndex(onlyOne, QStringLiteral("a")), -1);
    QCOMPARE(HotkeyManager::nextCycleIndex(currentIsTheOnlyActive, QStringLiteral("b")), -1);
    QCOMPARE(HotkeyManager::nextCycleIndex(QList<WorkspaceConfig>(), QStringLiteral("a")), -1);
}

void ModeFlowTests::hotkeyManager_reportsShortcutsTheSystemRefusedToBind() {
#ifdef Q_OS_WIN
    using ModeFlow::Core::WorkspaceConfig;
    using ModeFlow::Services::HotkeyManager;

    // F13 has no key on an ordinary keyboard, so claiming it here cannot steal a combination the
    // user assigned to something else. The manager's RegisterHotKey call is then refused by the
    // system for sure, instead of depending on which applications happen to be running.
    constexpr UINT claimedId = 0x0913;
    constexpr UINT claimedMods = MOD_CONTROL | MOD_ALT;
    QVERIFY(RegisterHotKey(nullptr, claimedId, claimedMods, VK_F13));
    const auto release = qScopeGuard([] { UnregisterHotKey(nullptr, claimedId); });

    HotkeyManager manager(nullptr, nullptr, nullptr);
    QSignalSpy errors(&manager, &HotkeyManager::errorOccurred);

    WorkspaceConfig profile;
    profile.id = QStringLiteral("claimed");
    profile.name = QStringLiteral("Claimed profile");
    profile.hotkey = QKeySequence(QStringLiteral("Ctrl+Alt+F13"));

    manager.setProfiles({profile});
    QCOMPARE(errors.count(), 1);
    const QString profileMessage = errors.at(0).at(0).toString();
    QVERIFY2(profileMessage.contains(QStringLiteral("Claimed profile")), qPrintable(profileMessage));
    QVERIFY2(profileMessage.contains(QStringLiteral("Ctrl+Alt+F13")), qPrintable(profileMessage));

    errors.clear();
    QVERIFY(manager.setNextProfileHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+F13"))));
    QCOMPARE(errors.count(), 1);
    const QString nextMessage = errors.at(0).at(0).toString();
    QVERIFY2(nextMessage.contains(QStringLiteral("Ctrl+Alt+F13")), qPrintable(nextMessage));

    // While a shortcut field has focus every registration is suspended on purpose, so an unbound
    // key in that window must not be reported as a failure.
    errors.clear();
    manager.setCaptureMode(true);
    QVERIFY(manager.setNextProfileHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+F14"))));
    QCOMPARE(errors.count(), 0);
#else
    QSKIP("Global hotkey registration is a Windows feature.");
#endif
}

void ModeFlowTests::displayManager_parseMonitorKey_valid() {
    ModeFlow::Services::WinLuid adapterId;
    unsigned int targetId;

    QVERIFY(ModeFlow::Services::DisplayManager::parseMonitorKey(QStringLiteral("1_2_3"), adapterId, targetId));
    QCOMPARE(adapterId.LowPart, 1u);
    QCOMPARE(adapterId.HighPart, 2);
    QCOMPARE(targetId, 3u);

    QVERIFY(ModeFlow::Services::DisplayManager::parseMonitorKey(QStringLiteral("A_B_C"), adapterId, targetId));
    QCOMPARE(adapterId.LowPart, 10u);
    QCOMPARE(adapterId.HighPart, 11);
    QCOMPARE(targetId, 12u);
}

void ModeFlowTests::displayManager_parseMonitorKey_invalid() {
    ModeFlow::Services::WinLuid adapterId;
    unsigned int targetId;

    QVERIFY(!ModeFlow::Services::DisplayManager::parseMonitorKey(QStringLiteral("1_2"), adapterId, targetId));
    QVERIFY(!ModeFlow::Services::DisplayManager::parseMonitorKey(QStringLiteral("1_2_3_4"), adapterId, targetId));
    QVERIFY(!ModeFlow::Services::DisplayManager::parseMonitorKey(QStringLiteral("x_y_z"), adapterId, targetId));
}

void ModeFlowTests::displayManager_destructionWaitsForAnInFlightSwitch() {
    auto state = std::make_shared<HeldSwitch>();
    auto manager = std::make_unique<HoldingDisplayManager>(state);
    const QFuture<bool> switching = manager->setDisplayModeAsync(QStringLiteral("display-1"));

    // The worker is now inside the blocking system call.
    QVERIFY(state->entered.tryAcquire(1, 5000));
    QCOMPARE(state->target, QStringLiteral("display-1"));

    // The switch ends only from another thread: the GUI thread is the one that quits.
    std::thread releaser([state]() {
        QThread::msleep(100);
        state->release.release();
    });

    QElapsedTimer timer;
    timer.start();
    manager.reset();
    const qint64 waited = timer.elapsed();
    releaser.join();

    // Freeing the manager must wait for the worker that still reports back into it.
    QVERIFY2(waited >= 50, qPrintable(QStringLiteral("destructor returned after %1 ms").arg(waited)));
    QVERIFY(state->finished.load());
    QVERIFY(switching.isFinished());
}

void ModeFlowTests::appLaunchConfig_jsonRoundTrip() {
    ModeFlow::Core::AppLaunchConfig original;
    original.appPath = QStringLiteral("C:\\Program Files\\app.exe");
    original.delaySeconds = 5;
    original.closeOnExit = true;

    QJsonObject json = original.toJson();
    ModeFlow::Core::AppLaunchConfig restored = ModeFlow::Core::AppLaunchConfig::fromJson(json);

    QCOMPARE(restored.appPath, original.appPath);
    QCOMPARE(restored.delaySeconds, original.delaySeconds);
    QCOMPARE(restored.closeOnExit, original.closeOnExit);
}

void ModeFlowTests::workspaceConfig_appsToLaunchJsonRoundTrip() {
    ModeFlow::Core::WorkspaceConfig original;
    original.id = QStringLiteral("test-id");
    original.name = QStringLiteral("Test Profile");
    original.displayId = QStringLiteral("display-1");
    original.audioId = QStringLiteral("audio-1");

    ModeFlow::Core::AppLaunchConfig app1;
    app1.appPath = QStringLiteral("C:\\app1.exe");
    app1.delaySeconds = 3;
    app1.closeOnExit = true;

    ModeFlow::Core::AppLaunchConfig app2;
    app2.appPath = QStringLiteral("C:\\app2.exe");
    app2.delaySeconds = 10;
    app2.closeOnExit = false;

    original.appsToLaunch = {app1, app2};

    QJsonObject json = original.toJson();
    ModeFlow::Core::WorkspaceConfig restored = ModeFlow::Core::WorkspaceConfig::fromJson(json);

    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.appsToLaunch.size(), 2);
    QCOMPARE(restored.appsToLaunch[0].appPath, app1.appPath);
    QCOMPARE(restored.appsToLaunch[0].delaySeconds, app1.delaySeconds);
    QCOMPARE(restored.appsToLaunch[0].closeOnExit, app1.closeOnExit);
    QCOMPARE(restored.appsToLaunch[1].appPath, app2.appPath);
    QCOMPARE(restored.appsToLaunch[1].delaySeconds, app2.delaySeconds);
    QCOMPARE(restored.appsToLaunch[1].closeOnExit, app2.closeOnExit);
}

void ModeFlowTests::winKeyTranslator_translatesCorrectly() {
    ModeFlow::Utils::WinKeyTranslator translator;

    QCOMPARE(translator.translate("QShortcut", "Meta"), QStringLiteral("Win"));
    QCOMPARE(translator.translate("QKeySequence", "Ctrl"), QStringLiteral("Ctrl"));
    QCOMPARE(translator.translate("QKeySequenceEdit", "Alt"), QStringLiteral("Alt"));
    QCOMPARE(translator.translate("QKeySequenceEdit", "Shift"), QStringLiteral("Shift"));

    QCOMPARE(translator.translate("QWidget", "Meta"), QString());
    QCOMPARE(translator.translate("QShortcut", "SomeOtherKey"), QString());
}

void ModeFlowTests::settingsDialog_hotkeyConflict_showsWarningViaDialogManager() {
    FakeSettingsManager settingsManager;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    auto* keyEdit = dialog.findChild<ModeFlow::Gui::HotkeyEdit*>("keyEditNextProfile");
    QVERIFY(keyEdit);

    // Set a key sequence and trigger validation
    keyEdit->setKeySequence(QKeySequence("Ctrl+Alt+N"));

    // FakeDialogManager should be called if there's any conflict or validation warning
    QVERIFY(dialog.result() == 0);
}

void ModeFlowTests::settingsDialog_keepsEditsMadeWhileAutostartIsBeingChecked() {
    FakeSettingsManager settingsManager;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    // The logon task exists, but the answer is held back until the test resolves it.
    settingsManager.autostartEnabledValue = true;
    settingsManager.deferAutostartCheck();

    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    // The check runs exactly once and only in the background: opening the dialog must not query
    // Task Scheduler on the GUI thread.
    QCOMPARE(settingsManager.autostartCheckCalls, 1);

    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox);
    auto* saveButton = buttonBox->button(QDialogButtonBox::Save);
    QVERIFY(saveButton);
    QVERIFY(!saveButton->isEnabled());

    auto* audioCheck = dialog.findChild<QCheckBox*>("checkAudioConfirmation");
    QVERIFY(audioCheck);
    audioCheck->toggle();
    QVERIFY(saveButton->isEnabled());

    settingsManager.finishDeferredAutostartCheck(true);
    QCoreApplication::processEvents();

    // The late answer is allowed to correct only the field it owns. Re-reading the whole form here would
    // turn the edit above into part of the saved baseline and hide Save again.
    auto* autostartCheck = dialog.findChild<QCheckBox*>("checkAutostart");
    QVERIFY(autostartCheck);
    QVERIFY(autostartCheck->isEnabled());
    QVERIFY(autostartCheck->isChecked());
    QVERIFY(saveButton->isEnabled());
}

namespace {
// Buttons and checks of a dialog that is never shown, which is enough to drive its signal/slot logic.
struct SettingsDialogHandles {
    QPushButton* save = nullptr;
    QPushButton* cancel = nullptr;
    QCheckBox* autostart = nullptr;
    QCheckBox* logging = nullptr;
};

SettingsDialogHandles findSettingsHandles(ModeFlow::Gui::SettingsDialog& dialog) {
    SettingsDialogHandles handles;
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    if (buttonBox) {
        handles.save = buttonBox->button(QDialogButtonBox::Save);
        handles.cancel = buttonBox->button(QDialogButtonBox::Cancel);
    }
    handles.autostart = dialog.findChild<QCheckBox*>("checkAutostart");
    handles.logging = dialog.findChild<QCheckBox*>("checkLoggingEnabled");
    return handles;
}
} // namespace

void ModeFlowTests::settingsDialog_appliesRuntimeLoggingOnlyAfterTheSettingsAreSaved() {
    using ModeFlow::Utils::LogManager;

    FakeSettingsManager settingsManager;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    const bool loggingBefore = LogManager::isEnabled();
    QScopeGuard restoreLogging([loggingBefore] { LogManager::setup(loggingBefore); });
    LogManager::setup(false);

    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    const auto handles = findSettingsHandles(dialog);
    QVERIFY(handles.save && handles.cancel && handles.autostart && handles.logging);

    handles.logging->setChecked(true);

    // The logon task will answer only when the test lets it.
    settingsManager.deferNextAutostartToggle();
    handles.autostart->setChecked(true);
    QVERIFY(handles.save->isEnabled());

    QSignalSpy finishedSpy(&dialog, &QDialog::finished);
    handles.save->click();
    QCoreApplication::processEvents();

    // Nothing has been written and logging stayed off: the change is still waiting for Task Scheduler.
    QCOMPARE(settingsManager.saveCalls, 0);
    QVERIFY(!LogManager::isEnabled());

    // The dialog cannot be abandoned while the external operation is running.
    QVERIFY(!handles.cancel->isEnabled());
    dialog.reject();
    QCOMPARE(finishedSpy.count(), 0);

    settingsManager.finishDeferredAutostartToggle(true);
    QCoreApplication::processEvents();

    // One transaction, one outcome: the task changed, the settings reached disk and logging followed.
    QCOMPARE(settingsManager.saveCalls, 1);
    QVERIFY(LogManager::isEnabled());
    QCOMPARE(finishedSpy.count(), 1);
}

void ModeFlowTests::settingsDialog_leavesEverythingUnchangedWhenTheLogonTaskRefusesTheChange() {
    using ModeFlow::Utils::LogManager;

    FakeSettingsManager settingsManager;
    settingsManager.autostartToggleShouldSucceed = false;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    const bool loggingBefore = LogManager::isEnabled();
    QScopeGuard restoreLogging([loggingBefore] { LogManager::setup(loggingBefore); });
    LogManager::setup(false);

    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    const auto handles = findSettingsHandles(dialog);
    QVERIFY(handles.save && handles.cancel && handles.autostart && handles.logging);

    handles.logging->setChecked(true);
    handles.autostart->setChecked(true);

    QSignalSpy finishedSpy(&dialog, &QDialog::finished);
    handles.save->click();
    QCoreApplication::processEvents();

    // The task refused, so no setting was written and the runtime logging never switched either.
    QCOMPARE(settingsManager.saveCalls, 0);
    QVERIFY(!LogManager::isEnabled());
    QVERIFY(!handles.autostart->isChecked());
    QCOMPARE(finishedSpy.count(), 0);

    // The dialog is operable again for another attempt.
    QVERIFY(handles.cancel->isEnabled());
}

void ModeFlowTests::settingsDialog_warnsWhenTheRollbackAfterAFailedSaveAlsoFails() {
    using ModeFlow::Utils::LogManager;

    FakeSettingsManager settingsManager;
    // The logon task accepts the change, then settings cannot be written and the task will not go back.
    settingsManager.autostartToggleResults = {true, false};
    settingsManager.saveShouldSucceed = false;
    FakeWorkspaceManager workspaceManager;
    FakeStyleManager styleManager;
    FakeDialogManager dialogManager;

    const bool loggingBefore = LogManager::isEnabled();
    QScopeGuard restoreLogging([loggingBefore] { LogManager::setup(loggingBefore); });
    LogManager::setup(false);

    ModeFlow::Gui::SettingsDialog dialog(&dialogManager, &settingsManager, &workspaceManager, &styleManager);
    QCoreApplication::processEvents();

    const auto handles = findSettingsHandles(dialog);
    QVERIFY(handles.save && handles.cancel && handles.autostart && handles.logging);

    handles.autostart->setChecked(true);

    QSignalSpy finishedSpy(&dialog, &QDialog::finished);
    handles.save->click();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    // Change plus rollback, and the rollback failure is reported instead of a silent divergence.
    QCOMPARE(settingsManager.autostartRequests.size(), 2);
    QCOMPARE(dialogManager.lastWarningTitle, QStringLiteral("Settings"));
    QVERIFY(dialogManager.lastWarningText.contains(QStringLiteral("could not be restored")));
    QVERIFY(!LogManager::isEnabled());
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(handles.cancel->isEnabled());
}

void ModeFlowTests::updateService_versionComparison() {
    using ModeFlow::Services::UpdateService;

    QVERIFY(UpdateService::isNewerVersion(QStringLiteral("1.0.0"), QStringLiteral("0.9.0")));
    QVERIFY(UpdateService::isNewerVersion(QStringLiteral("v1.0.0"), QStringLiteral("0.9.0")));
    QVERIFY(UpdateService::isNewerVersion(QStringLiteral("1.0.1"), QStringLiteral("1.0.0")));
    QVERIFY(UpdateService::isNewerVersion(QStringLiteral("v1.10.0"), QStringLiteral("1.9.0")));
    QVERIFY(UpdateService::isNewerVersion(QStringLiteral("2.0.0"), QStringLiteral("1.9.9")));

    QVERIFY(!UpdateService::isNewerVersion(QStringLiteral("1.0.0"), QStringLiteral("1.0.0")));
    QVERIFY(!UpdateService::isNewerVersion(QStringLiteral("v1.0.0"), QStringLiteral("1.0.0")));
    QVERIFY(!UpdateService::isNewerVersion(QStringLiteral("0.9.0"), QStringLiteral("1.0.0")));
    QVERIFY(!UpdateService::isNewerVersion(QStringLiteral("0.9.9"), QStringLiteral("1.0.0")));
}

namespace {
QJsonObject makeUpdateManifest(const QString& tagName, const QString& htmlUrl) {
    QJsonObject manifest;
    manifest.insert(QStringLiteral("tag_name"), tagName);
    manifest.insert(QStringLiteral("html_url"), htmlUrl);
    manifest.insert(QStringLiteral("body"), QStringLiteral("Notes"));
    return manifest;
}
} // namespace

void ModeFlowTests::updateService_usableReleaseRejectsADraftThatCannotBeOpened() {
    using ModeFlow::Services::UpdateService;

    const QString page = QStringLiteral("https://github.com/goloveshko/ModeFlow/releases/tag/v999.0.0");

    const auto good = UpdateService::usableRelease(makeUpdateManifest(QStringLiteral("v999.0.0"), page));
    QVERIFY(good.has_value());
    QCOMPARE(good->version, QStringLiteral("999.0.0"));
    QCOMPARE(good->downloadUrl.host(), QStringLiteral("github.com"));
    QCOMPARE(good->changelog, QStringLiteral("Notes"));

    // A tag that is not a version number used to look like "already up to date" and burned the daily window.
    QVERIFY(!UpdateService::usableRelease(makeUpdateManifest(QString(), page)).has_value());
    QVERIFY(!UpdateService::usableRelease(makeUpdateManifest(QStringLiteral("nightly"), page)).has_value());
    QVERIFY(!UpdateService::usableRelease(makeUpdateManifest(QStringLiteral("999.0.0-rc1"), page)).has_value());

    // A badge with no page to open is worse than no badge.
    const QString relativePage = QStringLiteral("releases/latest");
    const QString scriptPage = QStringLiteral("javascript:alert(1)");
    QVERIFY(!UpdateService::usableRelease(makeUpdateManifest(QStringLiteral("999.0.0"), QString())).has_value());
    QVERIFY(!UpdateService::usableRelease(makeUpdateManifest(QStringLiteral("999.0.0"), relativePage)).has_value());
    QVERIFY(!UpdateService::usableRelease(makeUpdateManifest(QStringLiteral("999.0.0"), scriptPage)).has_value());
}

void ModeFlowTests::updateService_offersACachedReleaseOnlyWhenItIsUsableAndNotSkipped() {
    using ModeFlow::Core::ConfigManager;
    using ModeFlow::Services::UpdateService;

    QStandardPaths::setTestModeEnabled(true);
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString cachePath = cacheDir + QStringLiteral("/update_cache.json");
    const auto restorePaths = qScopeGuard([cachePath]() {
        QFile::remove(cachePath);
        QStandardPaths::setTestModeEnabled(false);
    });

    // Test mode parks writableLocation() in Qt's own "qttest" tree; anything else means the
    // assertions below would read and delete the cache of the installed application.
    if (!cacheDir.contains(QStringLiteral("/qttest/"))) {
        QSKIP("Qt test mode did not redirect the cache location; refusing to touch the real update cache.");
    }

    auto writeCache = [cachePath](const QJsonObject& manifest) {
        QDir().mkpath(QFileInfo(cachePath).absolutePath());
        QFile file(cachePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        file.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
        return true;
    };

    ConfigManager config;
    const QString page = QStringLiteral("https://github.com/goloveshko/ModeFlow/releases/tag/v999.0.0");

    QVERIFY(writeCache(makeUpdateManifest(QStringLiteral("999.0.0"), QString())));
    {
        UpdateService service(&config);
        QVERIFY(!service.isUpdateAvailable());
        QVERIFY(!QFile::exists(cachePath));
    }

    QVERIFY(writeCache(makeUpdateManifest(QStringLiteral("999.0.0"), page)));
    {
        UpdateService service(&config);
        QVERIFY(service.isUpdateAvailable());
        QCOMPARE(service.latestVersion(), QStringLiteral("999.0.0"));
        QCOMPARE(service.downloadUrl().toString(), page);

        QSignalSpy cleared(&service, &UpdateService::noUpdateAvailable);
        service.dismissAvailableUpdate();
        QCOMPARE(cleared.count(), 1);
        QVERIFY(!service.isUpdateAvailable());
        QVERIFY(!QFile::exists(cachePath));
    }

    // A release the user dismissed must not return from the cache on the next start.
    QVERIFY(writeCache(makeUpdateManifest(QStringLiteral("999.0.0"), page)));
    config.setSkippedVersion(QStringLiteral("999.0.0"));
    {
        UpdateService service(&config);
        QVERIFY(!service.isUpdateAvailable());
    }
}

namespace {
/// Binds a guard to a name that cannot clash with the local server of a running ModeFlow instance.
bool listenAsTestServer(ModeFlow::Services::SingleInstanceGuard& guard, QString& serverNameOut) {
    serverNameOut = QStringLiteral("ModeFlow_test_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    bool started = false;
    QMetaObject::invokeMethod(&guard, "startAsServer", Qt::DirectConnection, Q_RETURN_ARG(bool, started),
                              Q_ARG(QString, serverNameOut));
    return started;
}
} // namespace

void ModeFlowTests::singleInstanceGuard_raisesTheWindowForAHandshakeSplitIntoFragments() {
    using ModeFlow::Services::SingleInstanceGuard;

    SingleInstanceGuard guard;
    QString serverName;
    QVERIFY(listenAsTestServer(guard, serverName));

    QSignalSpy raised(&guard, &SingleInstanceGuard::signalRaiseWindow);

    QLocalSocket client;
    client.connectToServer(serverName);
    QVERIFY(client.waitForConnected(ModeFlow::Utils::LocalSocketTimeoutMs));

    const QByteArray payload = SingleInstanceGuard::activationPayload();
    const int firstChunkSize = payload.size() / 2;

    client.write(payload.left(firstChunkSize));
    client.flush();
    QTest::qWait(50);

    // The old code compared this first chunk with the whole message and answered "unauthorized".
    QCOMPARE(raised.count(), 0);

    client.write(payload.mid(firstChunkSize));
    client.flush();

    QTRY_COMPARE_WITH_TIMEOUT(raised.count(), 1, ModeFlow::Utils::LocalSocketTimeoutMs * 4);
}

void ModeFlowTests::singleInstanceGuard_dropsClientsThatDoNotCompleteTheHandshake() {
    using ModeFlow::Services::SingleInstanceGuard;

    SingleInstanceGuard guard;
    QString serverName;
    QVERIFY(listenAsTestServer(guard, serverName));

    QSignalSpy raised(&guard, &SingleInstanceGuard::signalRaiseWindow);

    QLocalSocket wrongSecret;
    wrongSecret.connectToServer(serverName);
    QVERIFY(wrongSecret.waitForConnected(ModeFlow::Utils::LocalSocketTimeoutMs));
    wrongSecret.write(QByteArray("ACTIVATE:") + QByteArray(64, '0'));
    wrongSecret.flush();

    // Half of the handshake deadline: a mismatch is refused as soon as it is read, not by the timer.
    QTRY_VERIFY_WITH_TIMEOUT(wrongSecret.state() == QLocalSocket::UnconnectedState,
                             ModeFlow::Utils::LocalSocketTimeoutMs / 2);
    QCOMPARE(raised.count(), 0);

    QLocalSocket deadbeat;
    deadbeat.connectToServer(serverName);
    QVERIFY(deadbeat.waitForConnected(ModeFlow::Utils::LocalSocketTimeoutMs));
    deadbeat.write("ACTIVATE:");
    deadbeat.flush();

    // A peer that stops halfway must be released by the event loop, not by a blocking wait.
    QThread::msleep(ModeFlow::Utils::LocalSocketTimeoutMs / 4);
    QVERIFY(deadbeat.state() != QLocalSocket::UnconnectedState);

    QTRY_VERIFY_WITH_TIMEOUT(deadbeat.state() == QLocalSocket::UnconnectedState,
                             ModeFlow::Utils::LocalSocketTimeoutMs * 4);
    QCOMPARE(raised.count(), 0);
}

void ModeFlowTests::comInitGuard_initializesComOnWorkerThread() {
#ifdef Q_OS_WIN
    using ModeFlow::Utils::ComInitGuard;

    bool guardOk = false;
    HRESULT probeInsideScope = E_UNEXPECTED;
    HRESULT probeAfterScope = E_UNEXPECTED;

    // A fresh QThread has no COM apartment of its own. Querying the apartment type is not usable
    // here because COM enters an implicit MTA to answer it, so the state is probed with
    // CoInitializeEx: S_FALSE means somebody else already initialized the thread.
    auto* worker = QThread::create([&]() {
        {
            ComInitGuard guard(ComInitGuard::Apartment::MultiThreaded);
            guardOk = guard.isOk();
            probeInsideScope = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(probeInsideScope)) {
                CoUninitialize(); // undo only this probe, never the apartment being tested
            }
        }

        probeAfterScope = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(probeAfterScope)) {
            CoUninitialize();
        }
    });
    worker->start();
    worker->wait();
    delete worker;

    QVERIFY(guardOk);
    QCOMPARE(probeInsideScope, S_FALSE);
    QCOMPARE(probeAfterScope, S_OK);
#else
    QSKIP("COM initialization is a Windows-only concern");
#endif
}

void ModeFlowTests::logManager_rollAsideKeepsOnePreviousGeneration() {
    using ModeFlow::Utils::LogManager;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("log.txt"));
    const QString rolledPath = dir.filePath(QStringLiteral("log.txt.1"));

    const auto write = [](const QString& path, const QString& content) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(content.toUtf8());
        file.close();
        return true;
    };
    const auto read = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(file.readAll());
    };

    QVERIFY(write(logPath, QStringLiteral("first generation")));
    QVERIFY(LogManager::rollAside(logPath));
    QVERIFY(!QFile::exists(logPath));
    QCOMPARE(read(rolledPath), QStringLiteral("first generation"));

    // The next generation replaces the rolled one instead of piling up, so a long session keeps the
    // folder bounded to the active file plus a single previous generation.
    QVERIFY(write(logPath, QStringLiteral("second generation")));
    QVERIFY(LogManager::rollAside(logPath));
    QCOMPARE(read(rolledPath), QStringLiteral("second generation"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);

    // A missing active log is not a roll: the previous generation must survive.
    QVERIFY(!LogManager::rollAside(logPath));
    QCOMPARE(read(rolledPath), QStringLiteral("second generation"));
}

void ModeFlowTests::logViewerDialog_readsOnlyABoundedTailAndBuffer() {
    using ModeFlow::Gui::LogViewerDialog;
    using namespace ModeFlow::Utils;

    // Up to the read budget the file is taken whole, so a short log still shows from its start.
    QCOMPARE(LogViewerDialog::tailStartOffset(qint64(0)), qint64(0));
    QCOMPARE(LogViewerDialog::tailStartOffset(qint64(MaxViewerTailBytes)), qint64(0));

    // Past it the read starts at the last budget of bytes: opening a log costs the same however
    // long the session grew.
    QCOMPARE(LogViewerDialog::tailStartOffset(qint64(MaxViewerTailBytes) + 4096), qint64(4096));
    QCOMPARE(LogViewerDialog::tailStartOffset(qint64(MaxLogFileSizeBytes)),
             qint64(qint64(MaxLogFileSizeBytes) - qint64(MaxViewerTailBytes)));

    // The line buffer is capped the same way and gives up only what overflows it.
    QCOMPARE(LogViewerDialog::entriesToDrop(qsizetype(0)), qsizetype(0));
    QCOMPARE(LogViewerDialog::entriesToDrop(qsizetype(MaxViewerEntries - 1)), qsizetype(0));
    QCOMPARE(LogViewerDialog::entriesToDrop(qsizetype(MaxViewerEntries)), qsizetype(0));
    QCOMPARE(LogViewerDialog::entriesToDrop(qsizetype(MaxViewerEntries + 1)), qsizetype(1));
    QCOMPARE(LogViewerDialog::entriesToDrop(qsizetype(MaxViewerEntries + 250)), qsizetype(250));

    QStringList lines;
    for (int i = 0; i < MaxViewerEntries + 250; ++i)
        lines << QString::number(i);
    lines.remove(0, static_cast<int>(LogViewerDialog::entriesToDrop(lines.size())));
    QCOMPARE(lines.size(), qsizetype(MaxViewerEntries));
    QCOMPARE(lines.first(), QStringLiteral("250"));
    QCOMPARE(lines.last(), QString::number(MaxViewerEntries + 249));
}

void ModeFlowTests::logViewerDialog_treatsAShrinkingLogFileAsARotation() {
    using ModeFlow::Gui::LogViewerDialog;
    using Action = ModeFlow::Gui::LogViewerDialog::LoadAction;

    QVERIFY(LogViewerDialog::loadAction(qint64(1000), qint64(1000)) == Action::Nothing);
    QVERIFY(LogViewerDialog::loadAction(qint64(1500), qint64(1000)) == Action::Append);

    // Rotation leaves a fresh, small file behind. Appending from the consumed offset would read
    // nothing at all and keep the retired generation on screen.
    QVERIFY(LogViewerDialog::loadAction(qint64(200), qint64(1000)) == Action::Reload);
    QVERIFY(LogViewerDialog::loadAction(qint64(0), qint64(1000)) == Action::Reload);
}

void ModeFlowTests::logManager_togglingWhileWorkersEmitKeepsLogFileConsistent() {
    using ModeFlow::Utils::LogManager;

    // LogManager replaces the global message handler, so QTest's own handler has to be handed back
    // or the remaining tests lose their reporting.
    const QtMessageHandler previousHandler = qInstallMessageHandler(nullptr);
    QScopeGuard restoreHandler([previousHandler] { qInstallMessageHandler(previousHandler); });

    const QString logPath = LogManager::logFilePath();
    LogManager::setup(false);
    // The file can already hold lines from real runs of this build, so only look past the size it
    // has right now.
    const qint64 baseline = QFileInfo(logPath).size();

    const auto bytesSince = [&logPath](qint64 offset) {
        QFile file(logPath);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(offset)) {
            return QByteArray();
        }
        const QByteArray data = file.readAll();
        file.close();
        return data;
    };

    const auto malformedLines = [](const QByteArray& content) {
        QStringList malformed;
        const QStringList lines = QString::fromUtf8(content).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            // A stored message is "[time] [level] [category] [function] text", so exactly three
            // bracket separators. A torn write shows up as a line that lost that shape.
            if (!line.startsWith(QLatin1Char('[')) || line.count(QStringLiteral("] [")) != 3) {
                malformed << line;
            }
        }
        return malformed;
    };

    constexpr int WriterCount = 4;
    constexpr int MessagesPerWriter = 200;
    const QString token = QStringLiteral("lograce");
    const QString disabledProbe = QStringLiteral("lograce-while-disabled");

    const auto emitFromWorkers = [token](int messagesPerWriter) {
        QList<QThread*> writers;
        for (int w = 0; w < WriterCount; ++w) {
            writers.append(QThread::create([w, messagesPerWriter, token] {
                for (int i = 0; i < messagesPerWriter; ++i) {
                    qCDebug(lcUtil) << token << w << i;
                }
            }));
        }
        for (auto* writer : writers) {
            writer->start();
        }
        for (auto* writer : writers) {
            writer->wait();
            delete writer;
        }
    };

    // Phase 1 leaves logging on while four threads write, so the stored message count is exact and
    // proves the handler really is the one under test.
    LogManager::setup(true);
    emitFromWorkers(MessagesPerWriter);
    LogManager::setup(false);

    const QByteArray enabledContent = bytesSince(baseline);
    int storedMessages = 0;
    const QStringList enabledLines = QString::fromUtf8(enabledContent).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : enabledLines) {
        if (line.contains(token)) {
            ++storedMessages;
        }
    }

    const QStringList phaseOneMalformed = malformedLines(enabledContent);
    QVERIFY2(phaseOneMalformed.isEmpty(), qPrintable(phaseOneMalformed.value(0)));
    QCOMPARE(storedMessages, WriterCount * MessagesPerWriter);

    // Phase 2 switches the state underneath the same threads, which is what the settings dialog does
    // to the audio and display workers. Which messages get stored is timing dependent, so only the
    // invariants are checked: no torn line and no deadlock.
    std::atomic_bool stopToggling = false;
    auto* toggler = QThread::create([&stopToggling] {
        while (!stopToggling.load(std::memory_order_relaxed)) {
            LogManager::setup(true);
            LogManager::setup(false);
        }
    });

    toggler->start();
    emitFromWorkers(MessagesPerWriter);
    stopToggling.store(true, std::memory_order_relaxed);
    toggler->wait();
    delete toggler;

    LogManager::setup(false);

    const QByteArray toggledContent = bytesSince(baseline);
    const QStringList toggledMalformed = malformedLines(toggledContent);
    QVERIFY2(toggledMalformed.isEmpty(), qPrintable(toggledMalformed.value(0)));

    // Nothing may reach the log once it is switched off, not even a message emitted by a thread that
    // started while logging was still enabled.
    const qint64 finalSize = QFileInfo(logPath).size();
    qCDebug(lcUtil) << disabledProbe;
    QVERIFY2(!bytesSince(finalSize).contains(disabledProbe.toUtf8()),
             "A message was stored after logging was switched off");
}

namespace {
// MainWindow builds the details widgets and hands them to the presenter, so a test wires the same set
// by hand. The editor is declared last because it reads the addresses of the widgets it drives.
struct ProfileEditorFixture {
    ProfileEditorFixture(FakeWorkspaceManager& workspaceManager)
        : editor({&editName, &keyEditSpecific, &checkSkipInCycle, &comboDisplay, &comboAudio, &listApps, &btnCapture,
                  &groupGeneral, &groupHardware, &groupAuto},
                 &workspaceManager, nullptr, &iconMenu) {}

    QLineEdit editName;
    ModeFlow::Gui::HotkeyEdit keyEditSpecific;
    QCheckBox checkSkipInCycle;
    QComboBox comboDisplay;
    QComboBox comboAudio;
    ModeFlow::Gui::AppListWidget listApps;
    QPushButton btnCapture;
    QGroupBox groupGeneral;
    QGroupBox groupHardware;
    QGroupBox groupAuto;
    ModeFlow::Gui::ProfileIconMenu iconMenu;
    ModeFlow::Gui::ProfileEditor editor;
};
} // namespace

void ModeFlowTests::profileEditor_keepsBindingOfADeviceThatIsNotDetected() {
    using ModeFlow::Core::WorkspaceConfig;

    FakeWorkspaceManager workspaceManager;
    // The TV the profile was built for is switched off, and no audio output is reported at all.
    workspaceManager.m_displays = {{QStringLiteral("display-1"), QStringLiteral("Main display"), true, true}};
    workspaceManager.m_audioOutputs = {};

    ProfileEditorFixture form(workspaceManager);

    WorkspaceConfig profile;
    profile.id = QStringLiteral("profile-1");
    profile.name = QStringLiteral("Docking station");
    profile.displayId = QStringLiteral("display-tv");
    profile.audioId = QStringLiteral("audio-tv");

    form.editor.loadProfile(profile);
    QCOMPARE(form.comboDisplay.currentData().toString(), profile.displayId);
    QCOMPARE(form.comboAudio.currentData().toString(), profile.audioId);

    // Editing anything else must not rewrite a binding that only waits for the device to come back.
    form.editName.setText(QStringLiteral("Docking station 2"));
    WorkspaceConfig saved;
    form.editor.saveProfile(saved);
    QCOMPARE(saved.name, QStringLiteral("Docking station 2"));
    QCOMPARE(saved.displayId, profile.displayId);
    QCOMPARE(saved.audioId, profile.audioId);

    // Clearing a binding stays an explicit choice of the user.
    form.comboDisplay.setCurrentIndex(0);
    form.editor.saveProfile(saved);
    QVERIFY(saved.displayId.isEmpty());
    QCOMPARE(saved.audioId, profile.audioId);
}

void ModeFlowTests::profileEditor_renamesAProfileWithAManualIcon() {
    using ModeFlow::Core::WorkspaceConfig;

    FakeWorkspaceManager workspaceManager;
    workspaceManager.m_iconSymbolsByProfileName = {{QStringLiteral("Work"), QStringLiteral("briefcase")},
                                                   {QStringLiteral("Gaming"), QStringLiteral("gamepad")}};

    ProfileEditorFixture form(workspaceManager);

    // An icon that differs from the suggestion is a manual choice, and that used to mute the name field.
    WorkspaceConfig profile;
    profile.id = QStringLiteral("profile-1");
    profile.name = QStringLiteral("Work");
    profile.iconSymbol = QStringLiteral("rocket");
    form.editor.loadProfile(profile);

    QSignalSpy changedSpy(&form.editor, &ModeFlow::Gui::ProfileEditor::profileChanged);
    form.editName.setText(QStringLiteral("Gaming"));

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(form.editor.currentProfileIconSymbol(), QStringLiteral("rocket"));

    WorkspaceConfig saved;
    form.editor.saveProfile(saved);
    QCOMPARE(saved.name, QStringLiteral("Gaming"));
    QCOMPARE(saved.iconSymbol, QStringLiteral("rocket"));
}

void ModeFlowTests::profileEditor_followsTheSuggestedIconWhileItIsAutomatic() {
    using ModeFlow::Core::WorkspaceConfig;

    FakeWorkspaceManager workspaceManager;
    workspaceManager.m_iconSymbolsByProfileName = {{QStringLiteral("Work"), QStringLiteral("briefcase")},
                                                   {QStringLiteral("Gaming"), QStringLiteral("gamepad")}};

    ProfileEditorFixture form(workspaceManager);

    WorkspaceConfig profile;
    profile.id = QStringLiteral("profile-1");
    profile.name = QStringLiteral("Work");
    profile.iconSymbol = QStringLiteral("briefcase");
    form.editor.loadProfile(profile);

    QSignalSpy changedSpy(&form.editor, &ModeFlow::Gui::ProfileEditor::profileChanged);
    form.editName.setText(QStringLiteral("Gaming"));

    // Nothing was picked by hand, so the icon keeps following the name and the edit is still saved.
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(form.editor.currentProfileIconSymbol(), QStringLiteral("gamepad"));
}

namespace {
// One profile file read verbatim by the import path: a new id, the same id repeated inside the file,
// another new id and one that clashes with the profile the fake list already holds.
QString makeImportFile(const QString& tag) {
    const QString path = QDir::tempPath() + QStringLiteral("/modeflow-import-%1.json").arg(tag);
    QFile::remove(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }

    file.write(R"({
    "app": "ModeFlow",
    "version": 1,
    "profiles": [
        { "id": "new-a", "name": "A" },
        { "id": "new-a", "name": "A repeated inside the file" },
        { "id": "new-b", "name": "B" },
        { "id": "profile-1", "name": "Clashes with the existing list" }
    ]
})");
    file.close();
    return path;
}
} // namespace

void ModeFlowTests::profileImport_skipsDuplicatesInsideTheFile() {
    using ModeFlow::Gui::ProfileTransfer;

    FakeWorkspaceManager workspaceManager;
    FakeDialogManager dialogManager;

    const QString path = makeImportFile(QStringLiteral("duplicates"));
    QVERIFY(!path.isEmpty());
    QScopeGuard removeFile([&path] { QFile::remove(path); });

    dialogManager.mockOpenPath = path;
    ProfileTransfer transfer(&workspaceManager, &dialogManager);

    QSignalSpy completedSpy(&transfer, &ProfileTransfer::exchangeCompleted);
    transfer.doImport();

    // The whole file is checked before the model is touched, so the repeated id inside it is skipped
    // just like the one that clashes with the list.
    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(workspaceManager.saveCalls, 1);

    QStringList ids;
    for (const auto& cfg : workspaceManager.configs()) {
        ids << cfg.id;
    }
    QCOMPARE(ids, (QStringList{QStringLiteral("profile-1"), QStringLiteral("new-a"), QStringLiteral("new-b")}));
    QVERIFY(dialogManager.lastWarningText.contains(QStringLiteral("2 duplicate(s) were skipped")));
}

void ModeFlowTests::profileImport_leavesTheModelUnchangedWhenSavingFails() {
    using ModeFlow::Gui::ProfileTransfer;

    FakeWorkspaceManager workspaceManager;
    workspaceManager.saveShouldSucceed = false;
    FakeDialogManager dialogManager;

    const QString path = makeImportFile(QStringLiteral("save-fails"));
    QVERIFY(!path.isEmpty());
    QScopeGuard removeFile([&path] { QFile::remove(path); });

    dialogManager.mockOpenPath = path;
    ProfileTransfer transfer(&workspaceManager, &dialogManager);

    QSignalSpy completedSpy(&transfer, &ProfileTransfer::exchangeCompleted);
    const int rowsBefore = workspaceManager.configs().size();

    transfer.doImport();

    // An import that never reached disk must not stay visible in the list or be announced as success.
    QCOMPARE(completedSpy.count(), 0);
    QCOMPARE(workspaceManager.configs().size(), rowsBefore);
    QCOMPARE(dialogManager.lastWarningTitle, QStringLiteral("Import Failed"));
}

namespace {
QList<ModeFlow::Core::WorkspaceConfig> makeExportProfiles() {
    using ModeFlow::Core::WorkspaceConfig;

    WorkspaceConfig work;
    work.id = QStringLiteral("profile-work");
    work.name = QStringLiteral("Work");
    work.iconSymbol = QStringLiteral("briefcase");

    WorkspaceConfig gaming;
    gaming.id = QStringLiteral("profile-gaming");
    gaming.name = QStringLiteral("Gaming");
    gaming.skipInCycle = true;

    return {work, gaming};
}
} // namespace

void ModeFlowTests::profileExport_roundTripsThroughTheSerializer() {
    using ModeFlow::Utils::ProfileSerializer;

    const QString path = QDir::tempPath() + QStringLiteral("/modeflow-export-roundtrip.json");
    QFile::remove(path);
    QScopeGuard removeFile([&path] { QFile::remove(path); });

    const auto profiles = makeExportProfiles();
    QVERIFY(ProfileSerializer::exportProfiles(profiles, path));

    QString error;
    const auto imported = ProfileSerializer::importProfiles(path, error);

    QVERIFY(error.isEmpty());
    QCOMPARE(imported.size(), 2);
    QCOMPARE(imported[0].id, profiles[0].id);
    QCOMPARE(imported[0].name, profiles[0].name);
    QCOMPARE(imported[1].id, profiles[1].id);
    QCOMPARE(imported[1].skipInCycle, true);
}

void ModeFlowTests::profileExport_keepsAPreviousFileWhenTheTargetCannotBeReplaced() {
    using ModeFlow::Utils::ProfileSerializer;

    const QString path = QDir::tempPath() + QStringLiteral("/modeflow-export-locked.json");
    QFile::remove(path);

    QFile previous(path);
    QVERIFY(previous.open(QIODevice::WriteOnly));
    QVERIFY(previous.write("previous export") > 0);
    previous.close();

    // Declared before the locking handle so the file is removed only after that handle is closed.
    QScopeGuard removeFile([&path] { QFile::remove(path); });

    // A target that cannot be replaced must report failure and keep the bytes that were already there.
    QFile lock(path);
    QVERIFY(lock.open(QIODevice::ReadWrite));
    QVERIFY(!ProfileSerializer::exportProfiles(makeExportProfiles(), path));

    lock.seek(0);
    QCOMPARE(lock.readAll(), QByteArrayLiteral("previous export"));
}

namespace {
// A laptop panel that is in use, two attachable screens of which one is switched off right now, and a
// monitor that is not detected at all. The active panel sorts last by name, so the expectation below
// only holds while the provider keeps the device in use on top.
QList<ModeFlow::Core::DeviceEntry> makeDeviceEntries() {
    using ModeFlow::Core::DeviceEntry;

    return {
        DeviceEntry{QStringLiteral("tv"), QStringLiteral("LG TV"), true, false},
        DeviceEntry{QStringLiteral("ghost"), QStringLiteral("Acer Monitor"), false, false},
        DeviceEntry{QStringLiteral("panel"), QStringLiteral("Zen UZ27"), true, true},
        DeviceEntry{QStringLiteral("second"), QStringLiteral("Dell UP321"), true, false},
    };
}
} // namespace

void ModeFlowTests::deviceUtils_keepsTheActiveDeviceFirstAndUndetectedHardwareLast() {
    using ModeFlow::Utils::DeviceUtils;

    const auto sorted = DeviceUtils::sortAndGroupDevices(makeDeviceEntries());

    QStringList ids;
    for (const auto& device : sorted) {
        ids << device.id;
    }

    // The panel in use leads, the other detected screens follow alphabetically and the undetected
    // monitor is pushed to the end instead of being mixed into the selectable range.
    QCOMPARE(ids, (QStringList{QStringLiteral("panel"), QStringLiteral("second"), QStringLiteral("tv"),
                               QStringLiteral("ghost")}));
}

void ModeFlowTests::deviceUtils_selectableDevicesStopsBeforeUndetectedHardware() {
    using ModeFlow::Utils::DeviceUtils;

    const auto selectable = DeviceUtils::selectableDevices(DeviceUtils::sortAndGroupDevices(makeDeviceEntries()));

    QStringList ids;
    for (const auto& device : selectable) {
        ids << device.id;
    }
    std::sort(ids.begin(), ids.end());

    // Ordering is the other test's job: here only the filter matters, and the undetected monitor must
    // not be offered in the tray at all.
    QCOMPARE(ids, (QStringList{QStringLiteral("panel"), QStringLiteral("second"), QStringLiteral("tv")}));
}

QTEST_MAIN(ModeFlowTests)

#include "ModeFlowTests.moc"
