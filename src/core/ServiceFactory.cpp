#include "ServiceFactory.h"

#include <QStandardPaths>

#include "AppController.h"
#include "AppLauncher.h"
#include "AudioDeviceManager.h"
#include "AudioFeedbackService.h"
#include "AutostartManager.h"
#include "ConfigManager.h"
#include "DialogManager.h"
#include "DisplayManager.h"
#include "HotkeyManager.h"
#include "LocalizationManager.h"
#include "MainWindow.h"
#include "SettingsManager.h"
#include "StyleManager.h"
#include "TrayController.h"
#include "UpdateService.h"
#include "WindowLayouts.h"
#include "WorkspaceManager.h"
#include "WorkspaceService.h"

namespace ModeFlow::Core {

void ServiceFactory::createCoreServices(AppServices& services) {
    services.configManager = std::make_unique<ConfigManager>();
    services.locManager = std::make_unique<LocalizationManager>();
    services.appLauncher = std::make_unique<Services::AppLauncher>();
}

void ServiceFactory::createHardwareServices(AppServices& services, QObject* parent) {
    services.displayManager = std::make_unique<Services::DisplayManager>();
    services.audioManager = std::make_unique<Services::AudioDeviceManager>();
    services.autostartManager = std::make_unique<Services::AutostartManager>();
    services.audioFeedback = std::make_unique<Services::AudioFeedbackService>();

    services.styleManager = std::make_unique<StyleManager>(services.configManager->currentTheme(),
                                                           services.configManager->currentQtStyleKey());

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    services.windowBackend = std::make_unique<Services::SystemWindowBackend>();
    services.windowLayouts = std::make_unique<Services::WindowLayouts>(
        services.windowBackend.get(), appDataDir + QStringLiteral("/window_layouts.json"));

    services.workspaceService = std::make_unique<WorkspaceService>(
        services.displayManager.get(), services.audioManager.get(), services.appLauncher.get());
    services.workspaceService->setWindowLayouts(services.windowLayouts.get());
    services.workspaceService->setRememberWindowLayouts(services.configManager->rememberWindowLayouts());

    services.workspaceManager = std::make_unique<WorkspaceManager>(
        services.configManager.get(), services.displayManager.get(), services.audioManager.get());

    services.trayController = std::make_unique<Gui::TrayController>(services.workspaceManager.get());

    services.settingsManager =
        std::make_unique<SettingsManager>(services.configManager.get(), services.autostartManager.get(),
                                          services.locManager.get(), services.styleManager.get());

    services.hotkeyManager = std::make_unique<Services::HotkeyManager>(
        services.configManager.get(), services.displayManager.get(), services.audioManager.get());

    services.updateService = std::make_unique<Services::UpdateService>(services.configManager.get());

    services.dialogManager = std::make_unique<Gui::DialogManager>(services);
}

void ServiceFactory::createMainWindow(AppServices& services) {
    services.mainWindow =
        std::make_unique<Gui::MainWindow>(services.workspaceManager.get(), services.settingsManager.get(),
                                          services.styleManager.get(), services.dialogManager.get());
}

} // namespace ModeFlow::Core
