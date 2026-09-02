#pragma once

#include <QMessageBox>
#include <QObject>
#include <optional>

#include "ConfigTypes.h"

class QWidget;

namespace ModeFlow::Core {

/**
 * @brief Abstract facade (Dependency Inversion) for all application dialogs, alerts, and file pickers.
 */
class IDialogManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        ModeFlow::Core::ActiveDialog activeDialog READ activeDialog WRITE setActiveDialog NOTIFY activeDialogChanged)

public:
    explicit IDialogManager(QObject* parent = nullptr) : QObject(parent) {}
    ~IDialogManager() override = default;

    [[nodiscard]] virtual ActiveDialog activeDialog() const = 0;
    virtual void setActiveDialog(ActiveDialog dialog) = 0;

    // --- 1. Top-Level Modal App Windows ---
    virtual void showAboutDialog() = 0;
    virtual void showLogViewerDialog() = 0;
    virtual void showSettingsDialog() = 0;
    virtual void showUpdateDialog() = 0;

    virtual void forceUpdateCheck() = 0;

    // --- 2. Action Confirmations ---
    virtual bool confirmApplyProfile(const WorkspaceConfig& config) = 0;
    virtual bool confirmAction(const QString& title, const QString& text) = 0;
    virtual bool confirmAction(QWidget* parent, const QString& title, const QString& text) = 0;

    // --- 3. Message Box Alerts ---
    virtual void showInfo(const QString& title, const QString& text) = 0;
    virtual void showInfo(QWidget* parent, const QString& title, const QString& text) = 0;

    virtual void showWarning(const QString& title, const QString& text) = 0;
    virtual void showWarning(QWidget* parent, const QString& title, const QString& text) = 0;

    virtual void showError(const QString& title, const QString& text) = 0;
    virtual void showError(QWidget* parent, const QString& title, const QString& text) = 0;

    virtual int showMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                               const QString& informativeText = QString(), const QStringList& buttons = QStringList(),
                               int defaultButtonIndex = 0) = 0;

    // --- 4. File Dialog Pickers ---
    virtual QString getOpenFileName(const QString& caption, const QString& dir = QString(),
                                    const QString& filter = QString()) = 0;
    virtual QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir = QString(),
                                    const QString& filter = QString()) = 0;

    virtual QString getSaveFileName(const QString& caption, const QString& dir = QString(),
                                    const QString& filter = QString()) = 0;
    virtual QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir = QString(),
                                    const QString& filter = QString()) = 0;

    // --- 5. App Launch Configuration Dialog ---
    virtual std::optional<AppLaunchConfig> showAppLaunchDialog(const AppLaunchConfig* initialConfig = nullptr,
                                                               QWidget* parent = nullptr) = 0;

signals:
    void activeDialogChanged(ModeFlow::Core::ActiveDialog activeDialog);
    void settingsAccepted(const QString& oldLang, ModeFlow::Core::Theme oldTheme);
};

} // namespace ModeFlow::Core

Q_DECLARE_INTERFACE(ModeFlow::Core::IDialogManager, "com.ModeFlow.IDialogManager")
