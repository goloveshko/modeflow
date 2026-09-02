#include "ProfileTransfer.h"

#include "IDialogManager.h"
#include "IWorkspaceManager.h"
#include "ProfileSerializer.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

ProfileTransfer::ProfileTransfer(Core::IWorkspaceManager* wm, Core::IDialogManager* dialogManager, QObject* parent)
    : QObject(parent), m_workspaceManager(wm), m_dialogManager(dialogManager) {
    Q_ASSERT(m_workspaceManager);
    Q_ASSERT(m_dialogManager);
}

void ProfileTransfer::doImport() {
    QString filePath =
        m_dialogManager->getOpenFileName(tr("Import Profiles"), QString(), tr("JSON files (*.json);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QString error;
    auto imported = Utils::ProfileSerializer::importProfiles(filePath, error);

    if (!error.isEmpty()) {
        m_dialogManager->showWarning(tr("Import Failed"), error);
        return;
    }

    if (imported.isEmpty()) {
        m_dialogManager->showInfo(tr("Import"), tr("No profiles found in the file."));
        return;
    }

    auto existing = m_workspaceManager->configs();
    int addedCount = 0;
    int skippedCount = 0;

    for (const auto& cfg : imported) {
        bool isDuplicate = false;
        for (const auto& ex : existing) {
            if (ex.id == cfg.id) {
                isDuplicate = true;
                break;
            }
        }

        if (isDuplicate) {
            skippedCount++;
            continue;
        }

        m_workspaceManager->addConfig(cfg);
        addedCount++;
    }

    if (addedCount > 0) {
        m_workspaceManager->saveWorkspaces();
        emit exchangeCompleted();
    }

    QString msg = tr("Imported %1 profile(s).").arg(addedCount);
    if (skippedCount > 0) {
        msg += u"\n"_s + tr("%1 duplicate(s) were skipped.").arg(skippedCount);
    }
    m_dialogManager->showInfo(tr("Import Successful"), msg);
}

void ProfileTransfer::doExport() {
    QString filePath = m_dialogManager->getSaveFileName(tr("Export Profiles"), u"profiles.json"_s,
                                                        tr("JSON files (*.json);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    auto configs = m_workspaceManager->configs();
    if (Utils::ProfileSerializer::exportProfiles(configs, filePath)) {
        m_dialogManager->showInfo(tr("Export Successful"),
                                  tr("Exported %1 profile(s) to:\n%2").arg(configs.size()).arg(filePath));
    } else {
        m_dialogManager->showWarning(tr("Export Failed"), tr("Could not export profiles to:\n%1").arg(filePath));
    }
}

} // namespace ModeFlow::Gui
