#pragma once

#include <QKeySequence>
#include <QPointer>
#include <QString>

class QWidget;

namespace ModeFlow::Core {
class IDialogManager;
class IWorkspaceManager;
class ISettingsManager;
} // namespace ModeFlow::Core

namespace ModeFlow::Gui {

class HotkeyEdit;

/**
 * @brief Handles hotkey conflict detection and user validation alerts.
 */
class HotkeyValidator {
public:
    HotkeyValidator(Core::IWorkspaceManager* workspaceManager, Core::ISettingsManager* settingsManager,
                    Core::IDialogManager* dialogManager);

    // Validates the global "Next profile" hotkey
    bool validateNextProfileHotkey(HotkeyEdit* edit, QWidget* parent = nullptr);

    // Validates a specific profile hotkey
    bool validateProfileHotkey(HotkeyEdit* edit, const QString& currentProfileId, QWidget* parent = nullptr);

private:
    QString describeConflict(const QKeySequence& key, const QString& currentProfileId) const;
    bool applyChange(QPointer<HotkeyEdit> edit, const QKeySequence& newKey, const QKeySequence& oldKey,
                     const QString& conflictText, QWidget* parent);

    Core::IWorkspaceManager* m_workspaceManager;
    Core::ISettingsManager* m_settingsManager;
    Core::IDialogManager* m_dialogManager;
};

} // namespace ModeFlow::Gui