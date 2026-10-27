#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QThread>
#include <QtConcurrent>
#include <QtTest>

// Explicitly include required Qt and core types
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QPoint>
#include <QScopeGuard>
#include <QSize>
#include <QTextStream>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#ifdef Q_OS_WIN
// objidl.h carries the apartment enums used to probe COM state; objbase.h alone does not pull it in.
#include <objbase.h>
#include <objidl.h>
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
#include "DisplayManager.h"
#include "HotkeyEdit.h"
#include "HotkeyManager.h"
#include "IDialogManager.h"
#include "ISettingsManager.h"
#include "IStyleManager.h"
#include "IWorkspaceManager.h"
#include "LocalizationManager.h"
#include "LogManager.h"
#include "Logging.h"
#include "MainWindow.h"
#include "ProfileEditor.h"
#include "ProfileIconMenu.h"
#include "ProfileTransfer.h"
#include "SettingsDialog.h"
#include "SystemUtils.h"
#include "UpdateService.h"
#include "WorkspaceModel.h"
#include "WorkspaceService.h"

namespace {

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

    bool autostartEnabled() const override { return autostartEnabledValue; }

    QFuture<bool> autostartEnabledAsync() const override {
        return QtFuture::makeReadyValueFuture(autostartEnabledValue);
    }

    QFuture<bool> requestAutostartToggleAsync(bool enabled, int delay, bool enableLogging = false) override {
        autostartRequests.append({enabled, delay});
        if (!autostartToggleShouldSucceed) {
            return QtFuture::makeReadyValueFuture(false);
        }

        autostartEnabledValue = enabled;
        autostartDelaySeconds = delay;
        return QtFuture::makeReadyValueFuture(true);
    }

    int autostartDelay() const override { return autostartDelaySeconds; }
    void setAutostartDelay(int seconds) override { autostartDelaySeconds = seconds; }

    bool audioConfirmation() const override { return audioConfirmationEnabled; }
    void setAudioConfirmation(bool enabled) override { audioConfirmationEnabled = enabled; }

    bool autoUpdateEnabled() const override { return autoUpdateEnabledValue; }
    void setAutoUpdateEnabled(bool enabled) override { autoUpdateEnabledValue = enabled; }

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
    QPoint mainWindowPos() const override { return posMock; }
    void setMainWindowPos(const QPoint& pos) override { posMock = pos; }
    QSize mainWindowSize() const override { return sizeMock; }
    void setMainWindowSize(const QSize& size) override { sizeMock = size; }

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
    int autostartDelaySeconds = 5;
    bool audioConfirmationEnabled = true;
    bool autoUpdateEnabledValue = true;
    QKeySequence nextProfileSequence;
    ModeFlow::Core::StartupAction startupActionValue = ModeFlow::Core::StartupAction::None;
    QString startupProfileIdValue;
    ModeFlow::Core::Theme themeValue = ModeFlow::Core::Theme::Light;
    QString qtStyleKey = QStringLiteral("windows11");
    QList<QPair<bool, int>> autostartRequests;
    bool autostartToggleShouldSucceed = true;
    bool saveShouldSucceed = true;
    int saveCalls = 0;

    bool maximizedMock = false;
    QPoint posMock;
    QSize sizeMock = QSize(600, 450);
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

class FakeAppLauncherForWS : public ModeFlow::Services::AppLauncher {
    Q_OBJECT
public:
    using AppLauncher::AppLauncher;

    bool launchSequence(const QString& profileId, const QList<ModeFlow::Core::AppLaunchConfig>& apps) override {
        m_lastProfileId = profileId;
        m_lastApps = apps;
        ++m_launchSequenceCount;
        if (m_launchShouldFail) {
            emit errorOccurred(QStringLiteral("launch failed"));
            return false;
        }
        return true;
    }

    void cancelDelayedLaunches(const QString& profileId) override { m_cancelledProfileIds.append(profileId); }

    void closeProfileProcesses(const QString& profileId) override { m_closedProfileIds.append(profileId); }

    QString m_lastProfileId;
    QList<ModeFlow::Core::AppLaunchConfig> m_lastApps;
    int m_launchSequenceCount = 0;
    bool m_launchShouldFail = false;
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
    void settingsAcceptRollsBackOnSaveFailure();
    void commandLineBuilder_generatesCorrectArguments();
    void cliParser_parsesArgumentsCorrectly();
    void configManager_isThreadSafeAndConsistent();
    void configManager_skippedVersion_persistsCorrectly();

    void workspaceService_applyStatusName();
    void workspaceService_noChangeEmitsSuccess();
    void workspaceService_displayChangeConfirmed();
    void workspaceService_displayChangeTimeout();
    void workspaceService_displayChangeFailed();
    void workspaceService_audioSwitch();
    void workspaceService_audioErrorDegradesStatus();
    void workspaceService_appLaunchErrorDegradesStatus();
    void workspaceService_partialSuccessDisplayOnly();
    void workspaceService_audioConfirmationEmitted();
    void workspaceService_audioConfirmationDisabled();
    void workspaceService_secondRequestIsRefusedWhileSwitching();
    void workspaceService_staleDisplayFailureDoesNotFinishNextProfile();
    void workspaceService_directDisplaySwitchReportsFailure();
    void workspaceService_directDisplaySwitchBlocksProfileApply();
    void workspaceService_closesOutgoingAppsAfterDisplayIsConfirmed();
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

    void appLaunchConfig_jsonRoundTrip();
    void workspaceConfig_appsToLaunchJsonRoundTrip();

    void hotkeyManager_resolveInitialProfileId_null();
    void hotkeyManager_resolveInitialProfileId_selected();
    void hotkeyManager_resolveInitialProfileId_fallback();

    void displayManager_parseMonitorKey_valid();
    void displayManager_parseMonitorKey_invalid();

    void winKeyTranslator_translatesCorrectly();

    void settingsDialog_hotkeyConflict_showsWarningViaDialogManager();
    void profileEditor_keepsBindingOfADeviceThatIsNotDetected();
    void profileEditor_renamesAProfileWithAManualIcon();
    void profileEditor_followsTheSuggestedIconWhileItIsAutomatic();
    void profileImport_skipsDuplicatesInsideTheFile();
    void profileImport_leavesTheModelUnchangedWhenSavingFails();
    void updateService_versionComparison();
    void comInitGuard_initializesComOnWorkerThread();
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
        const auto [mode, options] = CliParser::parse(args);

        QCOMPARE(mode, CliParser::RunMode::NormalGui);
        QVERIFY(!options.isLogon);
        QVERIFY(!options.isSilentRestart);
        QVERIFY(!options.enableLogging);
        QCOMPARE(options.delaySeconds, 0);
    }

    // Scenario 2: Registration in Task Scheduler with Logs and 15-second Delay
    {
        QStringList args = {QStringLiteral("ModeFlow.exe"), QStringLiteral("--register"), QStringLiteral("--logon"),
                            QStringLiteral("--log"),        QStringLiteral("--delay"),    QStringLiteral("15")};
        const auto [mode, options] = CliParser::parse(args);

        QCOMPARE(mode, CliParser::RunMode::RegisterTask);
        QVERIFY(options.isLogon);
        QVERIFY(!options.isSilentRestart);
        QVERIFY(options.enableLogging);
        QCOMPARE(options.delaySeconds, 15);
    }

    // Scenario 3: Fallback safety when delay is passed as an invalid string
    {
        QStringList args = {QStringLiteral("ModeFlow.exe"), QStringLiteral("--silent-restart"),
                            QStringLiteral("--delay"), QStringLiteral("invalid")};
        const auto [mode, options] = CliParser::parse(args);

        QCOMPARE(mode, CliParser::RunMode::NormalGui);
        QVERIFY(options.isSilentRestart);
        QCOMPARE(options.delaySeconds, 0);
    }
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
    launcher.m_launchShouldFail = true;

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

namespace {
ModeFlow::Core::WorkspaceConfig makeProfileForSwitch(const QString& id, const QString& displayName) {
    ModeFlow::Core::WorkspaceConfig config;
    config.id = id;
    config.name = id;
    config.displayId = displayName;
    return config;
}
} // namespace

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
    const WorkspaceConfig second = makeProfileForSwitch(QStringLiteral("p2"), QStringLiteral("display-3"));

    service.applyWorkspaceConfig(first);
    QCOMPARE(startedSpy.count(), 1);

    // A tray or hotkey press during a transition must not take over the operation in flight.
    service.applyWorkspaceConfig(second);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(launcher.m_launchSequenceCount, 0);

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
    QCOMPARE(errorSpy.count(), 1);

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
    QCOMPARE(errorSpy.count(), 1);

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

namespace {
// Landing on a profile that launches applications is what creates applications to close later.
ModeFlow::Core::WorkspaceConfig profileWithApps(const QString& id, const QString& displayName) {
    ModeFlow::Core::WorkspaceConfig config;
    config.id = id;
    config.name = id;
    config.displayId = displayName;

    ModeFlow::Core::AppLaunchConfig app;
    app.appPath = QStringLiteral("C:\\Apps\\editor.exe");
    app.closeOnExit = true;
    config.appsToLaunch.append(app);

    return config;
}
} // namespace

void ModeFlowTests::workspaceService_closesOutgoingAppsAfterDisplayIsConfirmed() {
    using ModeFlow::Core::WorkspaceConfig;
    using Service = ModeFlow::Core::WorkspaceService;

    FakeDisplayManagerForWS display;
    display.m_currentDisplayKey = QStringLiteral("display-1");
    FakeAudioDeviceManagerForWS audio;
    FakeAppLauncherForWS launcher;

    Service service(&display, &audio, &launcher);
    service.setDisplayTimeout(5000);

    const WorkspaceConfig incoming = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"));
    service.applyWorkspaceConfig(incoming);
    QCOMPARE(launcher.m_launchSequenceCount, 1);
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
    QCOMPARE(launcher.m_launchSequenceCount, 1);
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

    const WorkspaceConfig incoming = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"));
    service.applyWorkspaceConfig(incoming);

    display.m_switchShouldFail = true;
    const WorkspaceConfig next = makeProfileForSwitch(QStringLiteral("pB"), QStringLiteral("display-2"));
    service.applyWorkspaceConfig(next);
    QTest::qWait(50);

    // The user is still on the old display, so nothing of profile A may be closed.
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);
    QCOMPARE(display.m_lastSwitchTarget, QStringLiteral("display-2"));
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

    const WorkspaceConfig incoming = profileWithApps(QStringLiteral("pA"), QStringLiteral("display-1"));
    service.applyWorkspaceConfig(incoming);
    QCOMPARE(launcher.m_launchSequenceCount, 1);

    service.setDisplayTimeout(30);
    const WorkspaceConfig next = makeProfileForSwitch(QStringLiteral("pB"), QStringLiteral("display-2"));
    service.applyWorkspaceConfig(next);
    QTest::qWait(120);

    // A switch that never reported back is not a confirmed move, so profile A keeps its windows.
    QCOMPARE(launcher.m_closedProfileIds.count(), 0);
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
} // namespace

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

    // The copied image stays locked while the process runs, so give it a moment to finish first.
    QTest::qWait(300);
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

QTEST_MAIN(ModeFlowTests)

#include "ModeFlowTests.moc"
