#include "ProfileTransfer.h"

#include <QSet>

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

    const auto existing = m_workspaceManager->configs();

    // The whole file is checked before anything reaches the model, so a repeated id inside the file is
    // skipped exactly like one that collides with the existing list.
    QSet<QString> takenIds;
    for (const auto& cfg : existing) {
        takenIds.insert(cfg.id);
    }

    QList<Core::WorkspaceConfig> accepted;
    int skippedCount = 0;

    for (const auto& cfg : imported) {
        if (takenIds.contains(cfg.id)) {
            ++skippedCount;
            continue;
        }

        takenIds.insert(cfg.id);
        accepted.append(cfg);
    }

    int addedCount = 0;
    if (!accepted.isEmpty()) {
        for (const auto& cfg : accepted) {
            m_workspaceManager->addConfig(cfg);
            ++addedCount;
        }

        if (!m_workspaceManager->saveWorkspaces()) {
            // A model holding profiles that never reached disk would show them until the next restart.
            for (int i = 0; i < addedCount; ++i) {
                m_workspaceManager->removeConfig(m_workspaceManager->configs().size() - 1);
            }

            m_dialogManager->showWarning(tr("Import Failed"),
                                         tr("Could not save the imported profiles; the list was left unchanged."));
            return;
        }

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
