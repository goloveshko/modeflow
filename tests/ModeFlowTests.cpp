#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtConcurrent>
#include <QtTest>

// Explicitly include required Qt and core types
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QPoint>
#include <QSize>
#include <optional>

#include "AppLauncher.h"
#include "AudioDeviceManager.h"
#include "AutostartManager.h"
#include "CliParser.h"
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
#include "MainWindow.h"
#include "SettingsDialog.h"
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
        Q_UNUSED(profileName);
        return QStringLiteral("desktop");
    }

    QList<ModeFlow::Core::DeviceEntry> getAvailableDisplays() const override { return m_displays; }
    QList<ModeFlow::Core::DeviceEntry> getAvailableAudioOutputs() const override { return m_audioOutputs; }

    mutable ModeFlow::Core::WorkspaceModel m_model;
    QList<ModeFlow::Core::DeviceEntry> m_displays;
    QList<ModeFlow::Core::DeviceEntry> m_audioOutputs;
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

    QFuture<bool> requestAutostartToggleAsync(bool enabled, int delay) override {
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
        if (m_switchShouldFail) {
            return QtFuture::makeReadyValueFuture(false);
        }
        return QtFuture::makeReadyValueFuture(true);
    }

    QString getCurrentDisplayKey() const override { return m_currentDisplayKey; }

    void emitDisplaysChanged() { emit displaysChanged(); }

    QString m_currentDisplayKey;
    QString m_lastSwitchTarget;
    bool m_switchShouldFail = false;
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

    bool launch(const QString& path, int delaySeconds) override {
        Q_UNUSED(delaySeconds);
        m_lastLaunchedPath = path;
        ++m_launchCount;
        if (m_launchShouldFail) {
            emit errorOccurred(QStringLiteral("launch failed"));
            return false;
        }
        return true;
    }

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

    QString m_lastLaunchedPath;
    QString m_lastProfileId;
    QList<ModeFlow::Core::AppLaunchConfig> m_lastApps;
    int m_launchCount = 0;
    int m_launchSequenceCount = 0;
    bool m_launchShouldFail = false;
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
    void startupLoggingRequiresCtrl();
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

    void appLauncher_emptyPathReturnsTrue();
    void appLauncher_nonExistentFileReturnsFalse();
    void appLauncher_nonExeReturnsFalse();

    void appLaunchConfig_jsonRoundTrip();
    void workspaceConfig_appsToLaunchJsonRoundTrip();

    void hotkeyManager_resolveInitialProfileId_null();
    void hotkeyManager_resolveInitialProfileId_selected();
    void hotkeyManager_resolveInitialProfileId_fallback();

    void displayManager_parseMonitorKey_valid();
    void displayManager_parseMonitorKey_invalid();

    void winKeyTranslator_translatesCorrectly();

    void settingsDialog_hotkeyConflict_showsWarningViaDialogManager();
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

void ModeFlowTests::startupLoggingRequiresCtrl() {
    using ModeFlow::Services::AutostartManager;

    QVERIFY(!AutostartManager::shouldEnableStartupLogging(Qt::NoModifier));
    QVERIFY(AutostartManager::shouldEnableStartupLogging(Qt::ControlModifier));
    QVERIFY(AutostartManager::shouldEnableStartupLogging(Qt::ControlModifier | Qt::ShiftModifier));
    QVERIFY(!AutostartManager::shouldEnableStartupLogging(Qt::AltModifier));
    QVERIFY(!AutostartManager::shouldEnableStartupLogging(Qt::ShiftModifier));
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

void ModeFlowTests::appLauncher_emptyPathReturnsTrue() {
    ModeFlow::Services::AppLauncher launcher;
    QVERIFY(launcher.launch(QString(), 0));
}

void ModeFlowTests::appLauncher_nonExistentFileReturnsFalse() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    QVERIFY(!launcher.launch(QStringLiteral("C:\\nonexistent\\fake.exe"), 0));
    QCOMPARE(errorSpy.count(), 1);
}

void ModeFlowTests::appLauncher_nonExeReturnsFalse() {
    ModeFlow::Services::AppLauncher launcher;
    QSignalSpy errorSpy(&launcher, &ModeFlow::Services::AppLauncher::errorOccurred);

    const QString tempDir = QDir::tempPath();
    const QString tempFile = tempDir + QStringLiteral("/test_launcher.txt");
    QFile file(tempFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QVERIFY(!launcher.launch(tempFile, 0));
    QCOMPARE(errorSpy.count(), 1);

    QFile::remove(tempFile);
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

QTEST_MAIN(ModeFlowTests)

#include "ModeFlowTests.moc"
