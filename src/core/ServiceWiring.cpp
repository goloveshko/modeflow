#include "ServiceWiring.h"

#include "AppController.h"
#include "AppLauncher.h"
#include "AudioDeviceManager.h"
#include "AudioFeedbackService.h"
#include "ConfigManager.h"
#include "DisplayManager.h"
#include "HotkeyManager.h"
#include "IDialogManager.h"
#include "LocalizationManager.h"
#include "Logging.h"
#include "MainWindow.h"
#include "TrayController.h"
#include "WorkspaceService.h"

namespace ModeFlow::Core {

void ServiceWiring::wireErrorConnections(AppServices& s, AppController* controller) {
    QObject::connect(s.configManager.get(), &ConfigManager::errorOccurred, controller,
                     &AppController::showNonBlockingError, Qt::UniqueConnection);
    QObject::connect(s.locManager.get(), &LocalizationManager::translationError, controller,
                     &AppController::showNonBlockingError, Qt::UniqueConnection);
    QObject::connect(s.appLauncher.get(), &Services::AppLauncher::errorOccurred, controller,
                     &AppController::showNonBlockingError, Qt::UniqueConnection);
}

void ServiceWiring::wireServiceConnections(AppServices& s, AppController* controller) {
    QObject::connect(s.audioManager.get(), &Services::AudioDeviceManager::errorOccurred, controller,
                     &AppController::showNonBlockingError, Qt::UniqueConnection);

    QObject::connect(s.workspaceService.get(), &WorkspaceService::errorOccurred, controller,
                     &AppController::showNonBlockingError, Qt::UniqueConnection);

    QObject::connect(s.hotkeyManager.get(), &Services::HotkeyManager::activateProfile, s.workspaceService.get(),
                     &WorkspaceService::applyWorkspaceConfig, Qt::UniqueConnection);

    auto* hotkeyManager = s.hotkeyManager.get();
    auto* configManager = s.configManager.get();

    QObject::connect(s.workspaceService.get(), &WorkspaceService::configApplyStarted, controller,
                     [hotkeyManager](const WorkspaceConfig&) { hotkeyManager->setSwitchInProgress(true); });

    QObject::connect(
        s.workspaceService.get(), &WorkspaceService::configApplyFinished, controller,
        [hotkeyManager, configManager](const WorkspaceConfig& config, WorkspaceService::ApplyStatus status) {
            hotkeyManager->setSwitchInProgress(false);

            if (status == WorkspaceService::ApplyStatus::Success) {
                configManager->setLastActiveProfileId(config.id);
                hotkeyManager->setActiveProfileId(config.id);
                return;
            }

            qCWarning(lcCore) << "Profile application finished with warnings:" << config.name
                              << "status =" << WorkspaceService::applyStatusName(status);
        });

    QObject::connect(s.workspaceService.get(), &WorkspaceService::requestAudioFeedback, s.audioFeedback.get(),
                     &Services::AudioFeedbackService::playConfirmation, Qt::UniqueConnection);

    auto* audioManager = s.audioManager.get();
    QObject::connect(s.audioManager.get(), &Services::AudioDeviceManager::defaultDeviceChanged, controller,
                     &AppController::onDefaultAudioDeviceChanged, Qt::UniqueConnection);

    QObject::connect(s.locManager.get(), &LocalizationManager::signalLanguageChanged, s.trayController.get(),
                     &Gui::TrayController::retranslateUi, Qt::UniqueConnection);

    QObject::connect(s.trayController.get(), &Gui::TrayController::showMainWindow, controller,
                     &AppController::raiseMainWindow, Qt::UniqueConnection);
    QObject::connect(s.trayController.get(), &Gui::TrayController::showSettingsDialog, s.dialogManager.get(),
                     &Core::IDialogManager::showSettingsDialog, Qt::UniqueConnection);

    QObject::connect(s.dialogManager.get(), &Core::IDialogManager::settingsAccepted, controller,
                     &AppController::handleSettingsChanges, Qt::UniqueConnection);

    QObject::connect(s.dialogManager.get(), &Core::IDialogManager::activeDialogChanged, s.trayController.get(),
                     &Gui::TrayController::activeDialogChanged, Qt::UniqueConnection);

    QObject::connect(s.trayController.get(), &Gui::TrayController::activateProfile, controller,
                     &AppController::confirmAndApplyProfile, Qt::UniqueConnection);

    auto* displayManager = s.displayManager.get();
    QObject::connect(s.trayController.get(), &Gui::TrayController::switchDisplay, displayManager,
                     [displayManager](const QString& displayId) { displayManager->setDisplayModeAsync(displayId); });

    QObject::connect(s.trayController.get(), &Gui::TrayController::switchAudio, s.audioManager.get(),
                     &Services::AudioDeviceManager::onSetDefaultOutputDevice, Qt::UniqueConnection);

    QObject::connect(s.trayController.get(), &Gui::TrayController::signalExitRequested, controller,
                     &AppController::requestAppExit);

    QObject::connect(controller, &AppController::activeDialogChanged, s.trayController.get(),
                     &Gui::TrayController::activeDialogChanged, Qt::UniqueConnection);
}

void ServiceWiring::wireWindowConnections(AppServices& s, AppController* controller) {
    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::activateProfile, controller,
                     &AppController::confirmAndApplyProfile, Qt::UniqueConnection);
    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::profilesChanged, controller, &AppController::profilesChanged,
                     Qt::UniqueConnection);
    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::hotkeyCaptureChanged, s.hotkeyManager.get(),
                     &Services::HotkeyManager::setCaptureMode, Qt::UniqueConnection);

    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::showSettingsDialog, s.dialogManager.get(),
                     &Core::IDialogManager::showSettingsDialog, Qt::UniqueConnection);
    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::showAboutDialog, s.dialogManager.get(),
                     &Core::IDialogManager::showAboutDialog, Qt::UniqueConnection);
    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::showUpdateDialog, s.dialogManager.get(),
                     &Core::IDialogManager::showUpdateDialog, Qt::UniqueConnection);
    QObject::connect(s.mainWindow.get(), &Gui::MainWindow::showLogViewer, s.dialogManager.get(),
                     &Core::IDialogManager::showLogViewerDialog, Qt::UniqueConnection);

    auto* window = s.mainWindow.get();
    QObject::connect(s.workspaceService.get(), &WorkspaceService::configApplyFinished, window,
                     [window](const WorkspaceConfig&, WorkspaceService::ApplyStatus status) {
                         if (status == WorkspaceService::ApplyStatus::Success) {
                             window->refreshVisualState();
                         }
                     });
}

} // namespace ModeFlow::Core
