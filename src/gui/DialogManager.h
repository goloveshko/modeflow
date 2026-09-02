#pragma once

#include <QList>

#include "IDialogManager.h"

class QWidget;

namespace ModeFlow::Core {
struct AppServices;
}

namespace ModeFlow::Gui {

/**
 * @brief Concrete Facade and Presenter for all application dialogs, alerts, and file pickers.
 */
class DialogManager : public Core::IDialogManager {
    Q_OBJECT
    Q_INTERFACES(ModeFlow::Core::IDialogManager)

public:
    explicit DialogManager(Core::AppServices& services, QObject* parent = nullptr);
    ~DialogManager() override;

    Core::ActiveDialog activeDialog() const override { return m_activeDialog; }
    void setActiveDialog(Core::ActiveDialog dialog) override;

    QWidget* parentWindow() const;

    // --- 1. Top-Level Modal App Windows ---
    void showAboutDialog() override;
    void showLogViewerDialog() override;
    void showSettingsDialog() override;
    void showUpdateDialog() override;

    void forceUpdateCheck() override;

    // --- 2. Action Confirmations ---
    bool confirmApplyProfile(const Core::WorkspaceConfig& config) override;
    bool confirmAction(const QString& title, const QString& text) override;
    bool confirmAction(QWidget* parent, const QString& title, const QString& text) override;

    // --- 3. Message Box Alerts ---
    void showInfo(const QString& title, const QString& text) override;
    void showInfo(QWidget* parent, const QString& title, const QString& text) override;

    void showWarning(const QString& title, const QString& text) override;
    void showWarning(QWidget* parent, const QString& title, const QString& text) override;

    void showError(const QString& title, const QString& text) override;
    void showError(QWidget* parent, const QString& title, const QString& text) override;

    int showMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                       const QString& informativeText = QString(), const QStringList& buttons = QStringList(),
                       int defaultButtonIndex = 0) override;

    // --- 4. File Dialog Pickers ---
    QString getOpenFileName(const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override;
    QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override;

    QString getSaveFileName(const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override;
    QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir = QString(),
                            const QString& filter = QString()) override;

    // --- 5. App Launch Configuration Dialog ---
    std::optional<Core::AppLaunchConfig> showAppLaunchDialog(const Core::AppLaunchConfig* initialConfig = nullptr,
                                                             QWidget* parent = nullptr) override;

private:
    struct DialogGuard {
        DialogManager* manager;
        DialogGuard(DialogManager* m, Core::ActiveDialog d) : manager(m) { manager->setActiveDialog(d); }
        ~DialogGuard() { manager->setActiveDialog(Core::ActiveDialog::None); }
    };

    QWidget* resolveParent(QWidget* parent) const;

    Core::AppServices& m_services;
    Core::ActiveDialog m_activeDialog = Core::ActiveDialog::None;
    QList<QMetaObject::Connection> m_manualUpdateConns;
};

} // namespace ModeFlow::Gui
