#include "TrayController.h"

#include "Constants.h"
#include "DeviceUtils.h"
#include "FontAwesome.h"
#include "IWorkspaceManager.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

TrayController::TrayController(Core::IWorkspaceManager* workspaceManager, QObject* parent)
    : QObject(parent), m_trayIcon(std::make_unique<QSystemTrayIcon>()), m_trayMenu(std::make_unique<QMenu>()),
      m_workspaceManager(workspaceManager) {
    m_trayIcon->setIcon(QIcon(u":/icons/app/icon.svg"_s));
    m_trayIcon->setContextMenu(m_trayMenu.get());

    connect(m_trayIcon.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            emit showMainWindow();
        }
    });

    connect(m_trayMenu.get(), &QMenu::aboutToShow, this, &TrayController::onMenuAboutToShow);

    rebuildMenu();

    m_trayIcon->show();
}

TrayController::~TrayController() {
    if (m_trayIcon)
        m_trayIcon->hide();
}

void TrayController::showNotification(const QString& title, const QString& message) {
    m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, Utils::TrayNotificationDurationMs);
}

void TrayController::rebuildMenu() {
    if (!m_trayMenu)
        return;

    m_trayMenu->clear();

    auto mainWindowAction = m_trayMenu->addAction(FontAwesome::icon(FontAwesome::Monitor, 16), tr("Open ModeFlow"));
    QFont boldFont = mainWindowAction->font();
    boldFont.setBold(true);
    mainWindowAction->setFont(boldFont);
    connect(mainWindowAction, &QAction::triggered, this, &TrayController::showMainWindow);

    auto* undoRestoreAction =
        m_trayMenu->addAction(FontAwesome::icon(FontAwesome::Undo, 16), tr("Undo window restore"));
    undoRestoreAction->setObjectName("undoWindowRestoreAction");
    undoRestoreAction->setVisible(m_windowRestoreUndoAvailable);
    connect(undoRestoreAction, &QAction::triggered, this, &TrayController::undoWindowRestoreRequested);

    m_trayMenu->addSeparator();

    m_profileMenu = std::make_unique<QMenu>(tr("Profiles"));
    m_profileMenu->setIcon(FontAwesome::icon(FontAwesome::FolderOpen, 16));
    m_trayMenu->addMenu(m_profileMenu.get());

    m_trayMenu->addSeparator();

    m_monitorMenu = std::make_unique<QMenu>(tr("Monitor"));
    m_monitorMenu->setIcon(FontAwesome::icon(FontAwesome::Monitor, 16));
    m_trayMenu->addMenu(m_monitorMenu.get());

    m_audioMenu = std::make_unique<QMenu>(tr("Sound"));
    m_audioMenu->setIcon(FontAwesome::icon(FontAwesome::Audio, 16));
    m_trayMenu->addMenu(m_audioMenu.get());

    m_trayMenu->addSeparator();

    auto settingsAction = m_trayMenu->addAction(FontAwesome::icon(FontAwesome::Settings, 16), tr("Settings..."));
    settingsAction->setObjectName("settingsAction");
    connect(settingsAction, &QAction::triggered, this, &TrayController::showSettingsDialog);

    m_trayMenu->addSeparator();

    auto quitAction = m_trayMenu->addAction(FontAwesome::icon(FontAwesome::PowerOff, 16), tr("Exit"));
    connect(quitAction, &QAction::triggered, this, &TrayController::signalExitRequested);
}

void TrayController::onMenuAboutToShow() {
    populateProfileSubmenu();
    populateMonitorSubmenu();
    populateAudioSubmenu();

    setEnabledAction("settingsAction", m_activeDialog == Core::ActiveDialog::None);
}

void TrayController::populateProfileSubmenu() {
    if (!m_profileMenu || !m_workspaceManager)
        return;

    m_profileMenu->clear();

    const auto configs = m_workspaceManager->configs();

    const int activeRowIndex = m_workspaceManager->activeRow();

    for (int i = 0; i < configs.size(); ++i) {
        const auto& cfg = configs[i];
        auto* action = m_profileMenu->addAction(cfg.name);
        action->setCheckable(true);
        action->setChecked(i == activeRowIndex);
        connect(action, &QAction::triggered, this, [this, cfg]() { emit activateProfile(cfg); });
    }
}

void TrayController::populateMonitorSubmenu() {
    if (!m_monitorMenu || !m_workspaceManager)
        return;

    auto devices = m_workspaceManager->getAvailableDisplays();

    populateDeviceSubmenu(m_monitorMenu.get(), devices, QString(),
                          [this](const QString& id) { emit switchDisplay(id); });
}

void TrayController::populateAudioSubmenu() {
    if (!m_audioMenu || !m_workspaceManager)
        return;

    auto devices = m_workspaceManager->getAvailableAudioOutputs();

    populateDeviceSubmenu(m_audioMenu.get(), devices, QString(), [this](const QString& id) { emit switchAudio(id); });
}

void TrayController::populateDeviceSubmenu(QMenu* menu, const QList<Core::DeviceEntry>& devices,
                                           const QString& currentId, std::function<void(const QString&)> emitFn) {
    if (!menu)
        return;
    menu->clear();

    for (const auto& device : Utils::DeviceUtils::selectableDevices(devices)) {
        auto* action = menu->addAction(device.name);
        action->setCheckable(true);
        action->setChecked(device.isActive);
        connect(action, &QAction::triggered, this, [emitFn, device]() { emitFn(device.id); });
    }
}

void TrayController::retranslateUi() {
    QMetaObject::invokeMethod(this, &TrayController::rebuildMenu, Qt::QueuedConnection);
}

void TrayController::setEnabledAction(const QString& actionStr, bool enabled) {
    if (auto* action = m_trayMenu->findChild<QAction*>(actionStr)) {
        action->setEnabled(enabled);
    }
}

void TrayController::activeDialogChanged(Core::ActiveDialog activeDialog) {
    m_activeDialog = activeDialog;
}

void TrayController::setWindowRestoreUndoAvailable(bool available) {
    m_windowRestoreUndoAvailable = available;

    if (auto* action = m_trayMenu->findChild<QAction*>(u"undoWindowRestoreAction"_s)) {
        action->setVisible(available);
    }
}

} // namespace ModeFlow::Gui
