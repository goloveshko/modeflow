#include "DialogManager.h"

#include <QMessageBox>
#include <QTimer>

#include "AboutDialog.h"
#include "AppLaunchDialog.h"
#include "AppServices.h"
#include "ConfigManager.h"
#include "HotkeyManager.h"
#include "LogViewerDialog.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "SettingsManager.h"
#include "StyleManager.h"
#include "UpdateDialog.h"
#include "UpdateService.h"
#include "VersionInfo.h"
#include "WorkspaceManager.h"

namespace ModeFlow::Gui {

DialogManager::DialogManager(Core::AppServices& services, QObject* parent)
    : Core::IDialogManager(parent), m_services(services) {}

DialogManager::~DialogManager() = default;

void DialogManager::setActiveDialog(Core::ActiveDialog dialog) {
    if (m_activeDialog == dialog)
        return;
    m_activeDialog = dialog;
    emit activeDialogChanged(dialog);
}

QWidget* DialogManager::parentWindow() const {
    return m_services.mainWindow ? m_services.mainWindow.get() : nullptr;
}

QWidget* DialogManager::resolveParent(QWidget* parent) const {
    return parent ? parent : parentWindow();
}

// --- 1. Top-Level Modal App Windows ---

void DialogManager::showAboutDialog() {
    if (m_activeDialog != Core::ActiveDialog::None)
        return;

    int result = 0;
    {
        m_services.styleManager->forceUnhover();
        DialogGuard guard(this, Core::ActiveDialog::About);

        const bool updateAvailable = m_services.updateService && m_services.updateService->isUpdateAvailable();
        const QString latestVersion = m_services.updateService ? m_services.updateService->latestVersion() : QString();

        AboutDialog dlg(m_services.styleManager.get(), updateAvailable, latestVersion, parentWindow());
        result = dlg.exec();
    }

    if (result == 2) {
        showUpdateDialog();
        QTimer::singleShot(0, this, &DialogManager::showAboutDialog);
    }
}

void DialogManager::showLogViewerDialog() {
    if (m_activeDialog != Core::ActiveDialog::None)
        return;

    m_services.styleManager->forceUnhover();
    DialogGuard guard(this, Core::ActiveDialog::LogViewer);

    LogViewerDialog dlg(this, m_services.settingsManager.get(), m_services.styleManager.get(), parentWindow());
    dlg.exec();
}

void DialogManager::showSettingsDialog() {
    if (m_activeDialog != Core::ActiveDialog::None)
        return;

    m_services.styleManager->forceUnhover();

    const QString oldLang = m_services.settingsManager->currentLanguage();
    const Core::Theme oldTheme = m_services.settingsManager->currentTheme();

    bool accepted = false;
    {
        DialogGuard guard(this, Core::ActiveDialog::Settings);

        SettingsDialog dlg(this, m_services.settingsManager.get(), m_services.workspaceManager.get(),
                           m_services.styleManager.get(), parentWindow());
        if (m_services.hotkeyManager) {
            connect(&dlg, &SettingsDialog::hotkeyCaptureChanged, m_services.hotkeyManager.get(),
                    &Services::HotkeyManager::setCaptureMode, Qt::UniqueConnection);
        }
        accepted = (dlg.exec() == QDialog::Accepted);
    }

    if (accepted) {
        emit settingsAccepted(oldLang, oldTheme);
    }
}

void DialogManager::showUpdateDialog() {
    if (m_activeDialog != Core::ActiveDialog::None)
        return;

    if (!m_services.updateService || !m_services.updateService->isUpdateAvailable())
        return;

    m_services.styleManager->forceUnhover();

    const QString version = m_services.updateService->latestVersion();
    const QString changelog = m_services.updateService->changelog();
    const QUrl downloadUrl = m_services.updateService->downloadUrl();

    {
        DialogGuard guard(this, Core::ActiveDialog::Update);

        UpdateDialog dlg(m_services.styleManager.get(), ModeFlow::Info::Version, version, changelog, downloadUrl,
                         parentWindow());
        const int result = dlg.exec();

        if (result == UpdateDialog::SkipVersionResult) {
            m_services.settingsManager->setSkippedVersion(version);

            if (m_services.mainWindow) {
                m_services.mainWindow->setUpdateAvailable(false, {});
            }
        }
    }
}

void DialogManager::forceUpdateCheck() {
    if (m_activeDialog != Core::ActiveDialog::None && m_activeDialog != Core::ActiveDialog::About)
        return;

    if (!m_services.updateService || m_services.updateService->isCheckingInProgress())
        return;

    // Disconnect any lingering connections from previous manual checks
    for (const auto& conn : m_manualUpdateConns) {
        QObject::disconnect(conn);
    }
    m_manualUpdateConns.clear();

    auto cleanupConns = [this]() {
        for (const auto& conn : m_manualUpdateConns) {
            QObject::disconnect(conn);
        }
        m_manualUpdateConns.clear();
    };

    const auto c1 = connect(m_services.updateService.get(), &Services::UpdateService::updateAvailable, this,
                            [this, cleanupConns](const QString&, const QUrl&, const QString&) {
                                cleanupConns();
                                showUpdateDialog();
                            });

    const auto c2 = connect(m_services.updateService.get(), &Services::UpdateService::noUpdateAvailable, this,
                            [this, cleanupConns]() {
                                cleanupConns();
                                if (m_services.mainWindow) {
                                    m_services.mainWindow->showToolTipOnMoreButton(tr("You are up to date."));
                                }
                            });

    const auto c3 =
        connect(m_services.updateService.get(), &Services::UpdateService::checkFailed, this,
                [this, cleanupConns](const QString& error) {
                    cleanupConns();
                    if (m_services.mainWindow) {
                        m_services.mainWindow->showToolTipOnMoreButton(tr("Update check failed: %1").arg(error));
                    }
                });

    m_manualUpdateConns = {c1, c2, c3};

    // Force network check on manual trigger
    m_services.updateService->checkForUpdates(true);
}

// --- 2. Action Confirmations ---

bool DialogManager::confirmApplyProfile(const Core::WorkspaceConfig& config) {
    if (!m_services.configManager->askConfirmation()) {
        return true;
    }

    m_services.styleManager->forceUnhover();
    const QString title = tr("Apply Profile");
    const QString text = tr("Apply profile '%1'?").arg(config.name);

    return m_services.styleManager->confirmAction(parentWindow(), title, text);
}

bool DialogManager::confirmAction(const QString& title, const QString& text) {
    return confirmAction(parentWindow(), title, text);
}

bool DialogManager::confirmAction(QWidget* parent, const QString& title, const QString& text) {
    m_services.styleManager->forceUnhover();
    return m_services.styleManager->confirmAction(resolveParent(parent), title, text);
}

// --- 3. Message Box Alerts ---

void DialogManager::showInfo(const QString& title, const QString& text) {
    showInfo(parentWindow(), title, text);
}

void DialogManager::showInfo(QWidget* parent, const QString& title, const QString& text) {
    m_services.styleManager->showInfo(resolveParent(parent), title, text);
}

void DialogManager::showWarning(const QString& title, const QString& text) {
    showWarning(parentWindow(), title, text);
}

void DialogManager::showWarning(QWidget* parent, const QString& title, const QString& text) {
    m_services.styleManager->showWarning(resolveParent(parent), title, text);
}

void DialogManager::showError(const QString& title, const QString& text) {
    showError(parentWindow(), title, text);
}

void DialogManager::showError(QWidget* parent, const QString& title, const QString& text) {
    m_services.styleManager->showError(resolveParent(parent), title, text);
}

int DialogManager::showMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                                  const QString& informativeText, const QStringList& buttons, int defaultButtonIndex) {
    return m_services.styleManager->showMessageBox(resolveParent(parent), icon, title, text, informativeText, buttons,
                                                   defaultButtonIndex);
}

// --- 4. File Dialog Pickers ---

QString DialogManager::getOpenFileName(const QString& caption, const QString& dir, const QString& filter) {
    return getOpenFileName(parentWindow(), caption, dir, filter);
}

QString DialogManager::getOpenFileName(QWidget* parent, const QString& caption, const QString& dir,
                                       const QString& filter) {
    return m_services.styleManager->getOpenFileName(resolveParent(parent), caption, dir, filter);
}

QString DialogManager::getSaveFileName(const QString& caption, const QString& dir, const QString& filter) {
    return getSaveFileName(parentWindow(), caption, dir, filter);
}

QString DialogManager::getSaveFileName(QWidget* parent, const QString& caption, const QString& dir,
                                       const QString& filter) {
    return m_services.styleManager->getSaveFileName(resolveParent(parent), caption, dir, filter);
}

std::optional<Core::AppLaunchConfig> DialogManager::showAppLaunchDialog(const Core::AppLaunchConfig* initialConfig,
                                                                        QWidget* parent) {
    AppLaunchDialog dialog(this, m_services.styleManager.get(), resolveParent(parent));

    if (initialConfig) {
        dialog.setAppConfig(*initialConfig);
        dialog.setWindowTitle(tr("Edit Program"));
    } else {
        dialog.setWindowTitle(tr("Add Program"));
    }

    if (dialog.exec() == QDialog::Accepted) {
        return dialog.appConfig();
    }

    return std::nullopt;
}

} // namespace ModeFlow::Gui