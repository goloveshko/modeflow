#include "HotkeyManager.h"

#include "AudioDeviceManager.h"
#include "ConfigManager.h"
#include "DisplayManager.h"
#include "Logging.h"

namespace ModeFlow::Services {

HotkeyManager::HotkeyManager(Core::ConfigManager* configManager, DisplayManager* displayManager,
                             AudioDeviceManager* audioManager, QObject* parent)
    : QObject(parent), m_configManager(configManager), m_displayManager(displayManager), m_audioManager(audioManager),
      m_activeProfileId(resolveInitialProfileId(configManager)) {
    m_nextProfileHotkey = new QHotkey(this);
    connect(m_nextProfileHotkey, &QHotkey::activated, this, &HotkeyManager::onNextProfileTriggered);
}

HotkeyManager::~HotkeyManager() {
    clearProfileHotkeys();
}

void HotkeyManager::clearProfileHotkeys() {
    for (const auto& mapping : m_profileMappings) {
        mapping.hotkey->deleteLater();
    }
    m_profileMappings.clear();
}

void HotkeyManager::updateRegistrations() {
    const bool shouldRegister = !m_captureMode;

    for (const auto& mapping : m_profileMappings) {
        if (!mapping.hotkey) {
            continue;
        }
        mapping.hotkey->setRegistered(shouldRegister);
    }

    if (m_nextProfileHotkey) {
        const bool hasShortcut = !m_nextProfileHotkey->shortcut().isEmpty();
        m_nextProfileHotkey->setRegistered(shouldRegister && hasShortcut);
    }
}

QString HotkeyManager::resolveInitialProfileId(Core::ConfigManager* cm) {
    if (!cm)
        return {};
    const auto selected = cm->selectedProfileId();
    return selected.isEmpty() ? cm->lastActiveProfileId() : selected;
}

int HotkeyManager::nextCycleIndex(const QList<Core::WorkspaceConfig>& configs, const QString& currentId) {
    if (configs.isEmpty()) {
        return -1;
    }

    int index = -1;
    for (int i = 0; i < configs.size(); ++i) {
        if (configs[i].id == currentId) {
            index = i;
            break;
        }
    }

    // Nothing active yet: the cycle starts at the first profile instead of re-activating nothing.
    index = (index < 0) ? 0 : (index + 1) % configs.size();

    const int startIndex = index;
    while (configs[index].skipInCycle) {
        index = (index + 1) % configs.size();
        if (index == startIndex) {
            return -1;
        }
    }

    return configs[index].id == currentId ? -1 : index;
}

int HotkeyManager::findProfileIndexById(const QString& id) const {
    for (int i = 0; i < m_lastConfigs.size(); ++i) {
        if (m_lastConfigs[i].id == id) {
            return i;
        }
    }
    return -1;
}

QString HotkeyManager::resolveCurrentProfileId() const {
    if (!m_activeProfileId.isEmpty() && findProfileIndexById(m_activeProfileId) != -1) {
        return m_activeProfileId;
    }

    if (m_configManager) {
        const QString lastActive = m_configManager->lastActiveProfileId();
        if (!lastActive.isEmpty() && findProfileIndexById(lastActive) != -1) {
            return lastActive;
        }
        const QString selected = m_configManager->selectedProfileId();
        if (!selected.isEmpty() && findProfileIndexById(selected) != -1) {
            return selected;
        }
    }

    if (m_displayManager && m_audioManager) {
        const QString currentDisplayId = m_displayManager->getCurrentDisplayKey();
        const QString currentAudioId = m_audioManager->getDefaultOutputDeviceId();

        int bestIndex = -1;
        int bestScore = 0;

        for (int i = 0; i < m_lastConfigs.size(); ++i) {
            const auto& cfg = m_lastConfigs[i];
            const bool displayMatches = !cfg.displayId.isEmpty() && cfg.displayId == currentDisplayId;
            const bool audioMatches = !cfg.audioId.isEmpty() && cfg.audioId == currentAudioId;

            int score = 0;
            if (displayMatches && audioMatches) {
                score = 3;
            } else if (displayMatches) {
                score = 2;
            } else if (audioMatches && cfg.displayId.isEmpty()) {
                score = 1;
            }

            if (score > bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }

        if (bestIndex != -1) {
            return m_lastConfigs[bestIndex].id;
        }
    }

    return m_lastConfigs.isEmpty() ? QString() : m_lastConfigs.first().id;
}

bool HotkeyManager::setProfiles(const QList<Core::WorkspaceConfig>& configs) {
    QList<HotkeyMapping> oldMappings = m_profileMappings;
    m_profileMappings.clear();
    m_lastConfigs = configs;

    bool anyChanges = false;
    QStringList takenShortcuts;

    for (const auto& cfg : configs) {
        if (cfg.hotkey.isEmpty()) {
            continue;
        }

        auto oldIt = std::find_if(oldMappings.begin(), oldMappings.end(), [&](const HotkeyMapping& m) {
            return m.config.id == cfg.id && m.hotkey->shortcut() == cfg.hotkey;
        });

        if (oldIt != oldMappings.end()) {
            // Reuse the existing QHotkey! Absolutely no Win32 RegisterHotKey overhead.
            m_profileMappings.append({oldIt->hotkey, cfg});
            oldMappings.erase(oldIt); // Remove from cleanup list to keep it alive
        } else {
            auto hotkey = new QHotkey(cfg.hotkey, true, this);
            if (hotkey->isRegistered()) {
                m_profileMappings.append({hotkey, cfg});
                connect(hotkey, &QHotkey::activated, this, &HotkeyManager::onProfileTriggered);
                qCDebug(lcService) << "Registered hotkey for profile:" << cfg.name << "key:" << cfg.hotkey.toString();
                anyChanges = true;
            } else {
                hotkey->deleteLater();
                qCWarning(lcService) << "Failed to register hotkey for profile:" << cfg.name
                                     << "key:" << cfg.hotkey.toString();
                takenShortcuts << QStringLiteral("%1 (%2)").arg(cfg.name, cfg.hotkey.toString());
            }
        }
    }

    if (!takenShortcuts.isEmpty()) {
        emit errorOccurred(tr("These shortcuts are already used by another application, so the profiles "
                              "will not react to them:\n%1")
                               .arg(takenShortcuts.join(u'\n')));
    }

    // Safely unregister and delete only the old hotkeys that are no longer used/edited
    for (const auto& oldMap : oldMappings) {
        if (oldMap.hotkey) {
            oldMap.hotkey->deleteLater();
            qCDebug(lcService) << "Unregistered obsolete hotkey for profile:" << oldMap.config.name;
            anyChanges = true;
        }
    }

    updateRegistrations();

    return anyChanges;
}

bool HotkeyManager::setNextProfileHotkey(const QKeySequence& sequence) {
    // Early Return: If the next profile shortcut sequence hasn't changed,
    // skip redundant OS unregistration/registration.
    if (m_nextProfileHotkey->shortcut() == sequence) {
        return false;
    }

    if (sequence.isEmpty()) {
        m_nextProfileHotkey->setShortcut(QKeySequence(), false);
        return true;
    }

    m_nextProfileHotkey->setShortcut(sequence, !m_captureMode);
    const bool registered = m_nextProfileHotkey->isRegistered();
    qCDebug(lcService) << "Registered global 'Next Profile' hotkey:" << sequence.toString() << "success:" << registered;
    // While a hotkey field has focus every registration is suspended on purpose, so an unbound
    // shortcut in that window is not a failure the user should be warned about.
    if (!m_captureMode && !registered) {
        emit errorOccurred(tr("The 'Next profile' shortcut %1 is already used by another application, so it will not "
                              "work.")
                               .arg(sequence.toString()));
    }
    return true;
}

void HotkeyManager::setCaptureMode(bool active) {
    if (m_captureMode == active) {
        return;
    }

    m_captureMode = active;
    updateRegistrations();
}

void HotkeyManager::setActiveProfileId(const QString& id) {
    m_activeProfileId = id;
}

void HotkeyManager::setSwitchInProgress(bool active) {
    m_switchInProgress = active;
}

void HotkeyManager::onProfileTriggered() {
    if (m_switchInProgress) {
        qCDebug(lcService) << "Ignoring profile hotkey while another switch is still in progress";
        return;
    }

    const QHotkey* hotkey = qobject_cast<const QHotkey*>(sender());
    if (!hotkey)
        return;

    for (const auto& mapping : m_profileMappings) {
        if (mapping.hotkey == hotkey) {
            emit activateProfile(mapping.config);
            break;
        }
    }
}

void HotkeyManager::onNextProfileTriggered() {
    if (m_lastConfigs.isEmpty())
        return;

    if (m_switchInProgress) {
        qCDebug(lcService) << "Ignoring next-profile hotkey while another switch is still in progress";
        return;
    }

    const QString currentId = resolveCurrentProfileId();
    const int nextIndex = nextCycleIndex(m_lastConfigs, currentId);
    if (nextIndex < 0) {
        qCDebug(lcService) << "Next-profile cycle has no other profile to activate, staying on" << currentId;
        return;
    }

    emit activateProfile(m_lastConfigs[nextIndex]);
}

} // namespace ModeFlow::Services
