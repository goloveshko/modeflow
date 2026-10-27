#pragma once

#include <QHotkey>
#include <QKeySequence>
#include <QList>
#include <QObject>

#include "ConfigTypes.h"

namespace ModeFlow::Core {
class ConfigManager;
}

namespace ModeFlow::Services {
class AudioDeviceManager;
class DisplayManager;

/**
 * @brief Manages global hotkey registration for workspace profiles.
 *
 * HotkeyManager handles registration and unregistration of system-wide hotkeys
 * for each workspace profile. It also manages a special "Next Profile" hotkey
 * that cycles through available profiles.
 *
 * Features:
 * - Automatic hotkey registration/deregistration
 * - Profile cycling with Next Profile hotkey
 * - Temporary suspend/resume while user edits a hotkey
 * - Proper cleanup in destructor
 *
 * Usage:
 * @code
 * HotkeyManager hotkeyManager(configManager);
 * hotkeyManager.setProfiles(workspaceConfigs);
 * hotkeyManager.setNextProfileHotkey(QKeySequence("Ctrl+Alt+N"));
 * @endcode
 *
 * @see WorkspaceConfig for profile configuration structure
 */
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a HotkeyManager.
     * @param configManager Configuration manager for last active profile queries.
     * @param parent Parent QObject.
     */
    explicit HotkeyManager(Core::ConfigManager* configManager, DisplayManager* displayManager,
                           AudioDeviceManager* audioManager, QObject* parent = nullptr);

    /**
     * @brief Destroys the HotkeyManager and cleans up all registered hotkeys.
     */
    ~HotkeyManager() override;

    /**
     * @brief Registers hotkeys for all workspace profiles.
     * @param configs List of workspace configurations with hotkey assignments.
     *
     * Clears all existing hotkey mappings and registers new ones based on
     * the hotkey field in each WorkspaceConfig. Profiles with empty hotkeys
     * are skipped.
     */
    bool setProfiles(const QList<Core::WorkspaceConfig>& configs);

    /**
     * @brief Sets the "Next Profile" cycling hotkey.
     * @param sequence Key sequence for the next profile action.
     *
     * An empty sequence disables the next profile hotkey.
     */
    bool setNextProfileHotkey(const QKeySequence& sequence);

    /**
     * @brief Temporarily unregisters all hotkeys while user captures a new shortcut.
     * @param active true to suspend all registrations, false to restore them.
     */
    void setCaptureMode(bool active);
    void setActiveProfileId(const QString& id);
    void setSwitchInProgress(bool active);

    static QString resolveInitialProfileId(Core::ConfigManager* cm);

    /**
     * @brief Which profile the next-profile hotkey should activate.
     * @param configs Profiles in cycle order, exactly as registered.
     * @param currentId Profile active now; empty when nothing is.
     * @return Index into @p configs, or -1 when there is no other profile to switch to.
     *
     * A cycle holding only the current profile, or only profiles marked skipInCycle, has to end
     * without a switch: re-emitting the current profile would close and relaunch its applications
     * even though no hardware moved.
     */
    static int nextCycleIndex(const QList<Core::WorkspaceConfig>& configs, const QString& currentId);

signals:
    /**
     * @brief Emitted when a profile hotkey is triggered.
     * @param config The workspace configuration to activate.
     */
    void activateProfile(const Core::WorkspaceConfig& config);

    /**
     * @brief Emitted when hotkeys are disabled and user attempts to use them.
     */
    void hotkeysDisabledWarning();

    /**
     * @brief Emitted when the system refused to bind a configured shortcut.
     */
    void errorOccurred(const QString& message);

private slots:
    void onProfileTriggered();
    void onNextProfileTriggered();

private:
    void clearProfileHotkeys();
    void updateRegistrations();
    int findProfileIndexById(const QString& id) const;
    QString resolveCurrentProfileId() const;

    struct HotkeyMapping {
        QHotkey* hotkey;
        Core::WorkspaceConfig config;
    };

    QList<HotkeyMapping> m_profileMappings;
    QHotkey* m_nextProfileHotkey = nullptr;

    Core::ConfigManager* m_configManager;
    DisplayManager* m_displayManager;
    AudioDeviceManager* m_audioManager;
    QList<Core::WorkspaceConfig> m_lastConfigs;
    QString m_activeProfileId;
    bool m_captureMode = false;
    bool m_switchInProgress = false;
};
} // namespace ModeFlow::Services
