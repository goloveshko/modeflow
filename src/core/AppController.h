#pragma once

#include <QTimer>

#include "AppServices.h"
#include "CliArgs.h"
#include "ConfigTypes.h"
#include "WorkspaceService.h"

namespace ModeFlow::Core {

class ServiceWiring;
class StartupPreflight;

/**
 * @brief Main application controller - coordinates all components.
 */
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(ActiveDialog activeDialog READ getActiveDialog WRITE setActiveDialog NOTIFY activeDialogChanged)

    friend class ServiceWiring;

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    void init(const StartupOptions& options);

    ActiveDialog getActiveDialog() const;
    void setActiveDialog(ActiveDialog activeDialog);

private:
    void finalizeInitialization();
    void setupAutoUpdateChecking();
    void applyAutoUpdateSetting();

    void applyStartupConfig();
    void ensureMainWindow();

    void handleSettingsChanges(const QString& oldLang, Core::Theme oldTheme);
    void processThemeChange(Core::Theme oldTheme);
    void restartApp(bool silentRestart = false);
    void showNonBlockingError(const QString& message);
    void reportApplyOutcome(const ModeFlow::Core::WorkspaceConfig& config,
                            ModeFlow::Core::WorkspaceService::ApplyStatus status, const QString& issues);

public slots:
    void raiseMainWindow();
    void requestAppExit();
    void confirmAndApplyProfile(const ModeFlow::Core::WorkspaceConfig& config);

private slots:
    void profilesChanged();
    void handleStartupSuccess();
    void handleStartupError();
    void onDefaultAudioDeviceChanged(const QString& id);
    void onAboutToQuit();

signals:
    void activeDialogChanged(ActiveDialog activeDialog);
    void aboutToRestart();

private:
    AppServices m_services;

    std::unique_ptr<StartupPreflight> m_preflight;
    QTimer m_updateTimer;

    QString m_pendingUpdateVersion;

    ActiveDialog m_activeDialog = ActiveDialog::None;
    bool m_restartPending = false;
    bool m_isFinalizing = false;

    StartupOptions m_options;
};
} // namespace ModeFlow::Core
