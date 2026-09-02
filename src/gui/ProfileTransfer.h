#pragma once

#include <QObject>

namespace ModeFlow::Core {
class IDialogManager;
class IWorkspaceManager;
} // namespace ModeFlow::Core

namespace ModeFlow::Gui {

/**
 * @brief Controller responsible for managing profile import/export operations.
 * Uses the IDialogManager interface as a unified UI facade for file dialogs and status alerts.
 */
class ProfileTransfer : public QObject {
    Q_OBJECT
public:
    explicit ProfileTransfer(Core::IWorkspaceManager* wm, Core::IDialogManager* dialogManager,
                             QObject* parent = nullptr);
public slots:
    void doImport();
    void doExport();

signals:
    void exchangeCompleted();

private:
    Core::IWorkspaceManager* m_workspaceManager;
    Core::IDialogManager* m_dialogManager;
};

} // namespace ModeFlow::Gui
