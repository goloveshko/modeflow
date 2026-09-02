#include "HotkeyValidator.h"

#include <QCoreApplication>
#include <QStringBuilder>

#include "HotkeyEdit.h"
#include "IDialogManager.h"
#include "ISettingsManager.h"
#include "IWorkspaceManager.h"

using namespace Qt::StringLiterals;

namespace ModeFlow::Gui {

HotkeyValidator::HotkeyValidator(Core::IWorkspaceManager* workspaceManager, Core::ISettingsManager* settingsManager,
                                 Core::IDialogManager* dialogManager)
    : m_workspaceManager(workspaceManager), m_settingsManager(settingsManager), m_dialogManager(dialogManager) {
    Q_ASSERT(m_workspaceManager);
    Q_ASSERT(m_settingsManager);
    Q_ASSERT(m_dialogManager);
}

bool HotkeyValidator::validateNextProfileHotkey(HotkeyEdit* edit, QWidget* parent) {
    if (!edit)
        return false;

    const QKeySequence currentKey = edit->keySequence();
    const QKeySequence baseKey = edit->lastAcceptedKey();

    if (currentKey == baseKey)
        return false;

    const QString conflictText = describeConflict(currentKey, QString());
    bool accepted = applyChange(edit, currentKey, baseKey, conflictText, parent);

    if (accepted) {
        edit->setLastAcceptedKey(currentKey);
    }

    return accepted;
}

bool HotkeyValidator::validateProfileHotkey(HotkeyEdit* edit, const QString& currentProfileId, QWidget* parent) {
    if (!edit)
        return false;

    const QKeySequence currentKey = edit->keySequence();
    const QKeySequence baseKey = edit->lastAcceptedKey();

    if (currentKey == baseKey)
        return false;

    const QString conflictText = describeConflict(currentKey, currentProfileId);
    bool accepted = applyChange(edit, currentKey, baseKey, conflictText, parent);

    if (accepted) {
        edit->setLastAcceptedKey(currentKey);
    }

    return accepted;
}

QString HotkeyValidator::describeConflict(const QKeySequence& key, const QString& currentProfileId) const {
    if (key.isEmpty())
        return {};

    const QString keyName = key.toString(QKeySequence::NativeText);
    const QKeySequence nextProfileHotkey = m_settingsManager->nextProfileHotkey();

    if (!nextProfileHotkey.isEmpty() && key == nextProfileHotkey) {
        return QCoreApplication::translate("HotkeyValidator", "Shortcut %1 is already assigned to 'Next profile'.")
            .arg(keyName);
    }

    const auto configs = m_workspaceManager->configs();
    for (const auto& cfg : configs) {
        if (cfg.id == currentProfileId || cfg.hotkey.isEmpty()) {
            continue;
        }

        if (key == cfg.hotkey) {
            return QCoreApplication::translate("HotkeyValidator", "Shortcut %1 is already assigned to profile '%2'.")
                .arg(keyName, cfg.name);
        }
    }

    return {};
}

bool HotkeyValidator::applyChange(QPointer<HotkeyEdit> edit, const QKeySequence& newKey, const QKeySequence& oldKey,
                                  const QString& conflictText, QWidget* parent) {
    if (conflictText.isEmpty())
        return true;

    if (!edit)
        return false;

    edit->setValidating(true);

    {
        QSignalBlocker blocker(edit);
        const QString displayMessage =
            conflictText % u"\n\n"_sv %
            QCoreApplication::translate("HotkeyValidator",
                                        "This shortcut cannot be used here. Reverting to previous value.");

        if (m_dialogManager) {
            m_dialogManager->showWarning(parent, QCoreApplication::translate("HotkeyValidator", "Hotkey Conflict"),
                                         displayMessage);
        }
        edit->setKeySequence(oldKey);
    }

    edit->setValidating(false);

    return false;
}

} // namespace ModeFlow::Gui