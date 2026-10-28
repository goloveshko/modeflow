#include "WorkspaceService.h"

#include <QTimer>
#include <QtConcurrent>

#include <utility>

#include "AppLauncher.h"
#include "AudioDeviceManager.h"
#include "ConfigManager.h"
#include "Constants.h"
#include "DisplayManager.h"
#include "Logging.h"
#include "WindowLayouts.h"

using namespace Qt::StringLiterals;

namespace ModeFlow::Core {

namespace {
bool hasFollowUpActions(const WorkspaceConfig& config) {
    return !config.audioId.isEmpty() || !config.appsToLaunch.isEmpty();
}
} // namespace

WorkspaceService::WorkspaceService(Services::DisplayManager* displayManager, Services::AudioDeviceManager* audioManager,
                                   Services::AppLauncher* appLauncher, QObject* parent)
    : QObject(parent), m_displayManager(displayManager), m_audioManager(audioManager), m_appLauncher(appLauncher),
      m_displayTimeoutTimer(std::make_unique<QTimer>()), m_windowRestoreTimer(std::make_unique<QTimer>()),
      m_undoExpiryTimer(std::make_unique<QTimer>()), m_displayTimeoutMs(Utils::DisplaySwitchTimeoutMs),
      m_undoTimeoutMs(Utils::WindowRestoreUndoAvailableMs) {
    if (!displayManager || !audioManager || !appLauncher) {
        qCFatal(lcCore) << "WorkspaceService: Dependencies must not be null";
        return;
    }

    m_displayTimeoutTimer->setSingleShot(true);
    m_windowRestoreTimer->setSingleShot(true);
    m_undoExpiryTimer->setSingleShot(true);

    connect(m_displayTimeoutTimer.get(), &QTimer::timeout, this, &WorkspaceService::onDisplayTimeout);
    connect(m_windowRestoreTimer.get(), &QTimer::timeout, this, &WorkspaceService::restoreWindowLayout);
    connect(m_undoExpiryTimer.get(), &QTimer::timeout, this, &WorkspaceService::clearWindowRestoreUndo);

    // Monitor system display changes to verify if our switch was successful
    connect(m_displayManager, &Services::DisplayManager::displaysChanged, this, &WorkspaceService::onDisplayChanged);
    connect(m_audioManager, &Services::AudioDeviceManager::errorOccurred, this, [this](const QString&) {
        if (m_isApplyingProfile) {
            m_hadAudioError = true;
        }
    });
    connect(m_appLauncher, &Services::AppLauncher::errorOccurred, this, [this](const QString&) {
        if (m_isApplyingProfile) {
            m_hadAppLaunchError = true;
        }
    });
}

void WorkspaceService::setDisplayTimeout(int ms) {
    m_displayTimeoutMs = ms;
}

void WorkspaceService::setAudioConfirmation(bool enabled) {
    m_audioConfirmation = enabled;
}

void WorkspaceService::applyPreferences(const ConfigManager& config) {
    m_audioConfirmation = config.audioConfirmation();
    m_rememberWindowLayouts = config.rememberWindowLayouts();
}

void WorkspaceService::setWindowLayouts(Services::WindowLayouts* layouts) {
    m_windowLayouts = layouts;
}

void WorkspaceService::setRememberWindowLayouts(bool enabled) {
    m_rememberWindowLayouts = enabled;
}

void WorkspaceService::setWindowRestoreUndoTimeout(int ms) {
    m_undoTimeoutMs = ms;
}

void WorkspaceService::undoWindowRestore() {
    if (m_undoLayout.isEmpty() || m_windowLayouts == nullptr) {
        return;
    }

    const QList<Services::WindowPlacement> snapshot = std::move(m_undoLayout);
    m_undoLayout.clear();
    m_undoExpiryTimer->stop();
    emit windowRestoreUndoAvailable(false);

    const int placed = m_windowLayouts->restore(snapshot);
    qCDebug(lcCore) << "Window restore undone:" << placed << "of" << snapshot.size() << "windows placed.";
}

void WorkspaceService::applyWorkspaceConfig(const WorkspaceConfig& config) {
    if (isSwitchInProgress()) {
        qCWarning(lcCore) << "Another switch is in progress, ignoring profile:" << config.name;
        emit errorOccurred(tr("A display or profile switch is already in progress."));
        return;
    }

    qCDebug(lcCore) << "Applying profile:" << config.name;

    m_displayTimeoutTimer->stop();
    // A restore that has not run yet belongs to the switch this request replaces, and its undo
    // would now move windows across a topology the user has already left.
    m_windowRestoreTimer->stop();
    clearWindowRestoreUndo();
    m_isWaitingForDisplay = false;
    m_isApplyingProfile = true;
    const quint64 operationId = ++m_operationId;

    // A delayed launch must not appear on the workspace the user is leaving, so scheduled timers go
    // away right away. Applications that are already running wait for the display switch itself.
    // Re-applying the profile that is already current keeps them: cancelling and re-scheduling would
    // delay an application that has not started yet, and its own list is filtered below.
    m_outgoingProfileId = m_pendingConfig.id;
    if (!m_outgoingProfileId.isEmpty() && m_outgoingProfileId != config.id) {
        m_appLauncher->cancelDelayedLaunches(m_outgoingProfileId);
    }

    m_pendingConfig = config;

    QString currentDisplay = m_displayManager->getCurrentDisplayKey();
    QString currentAudio = m_audioManager->getDefaultOutputDeviceId();

    m_displaySwitchRequested = !config.displayId.isEmpty() && (config.displayId != currentDisplay);
    const bool audioChanged = !config.audioId.isEmpty() && (config.audioId != currentAudio);

    m_displayChangeSucceeded = false;
    m_hadAudioError = false;
    m_hadAppLaunchError = false;
    m_stageIssues.clear();
    m_hadActualChanges = m_displaySwitchRequested || audioChanged;

    // The layout of the screens being left is what the next visit to them should bring back, so it is
    // taken before the system has a chance to move anything.
    if (m_displaySwitchRequested) {
        rememberCurrentWindowLayout();
    }

    emit configApplyStarted(config);

    if (!m_displaySwitchRequested) {
        finalizeApplication(ApplyStatus::Success);
        return;
    }

    m_isWaitingForDisplay = true;
    m_displayTimeoutTimer->start(m_displayTimeoutMs);
    qCDebug(lcService) << "Display switch command sent asynchronously, waiting for system confirmation...";

    m_displayManager->setDisplayModeAsync(config.displayId).then(this, [this, config, operationId](bool success) {
        // A switch that already timed out is no longer this operation's concern: reporting it here
        // would finish the profile that is being applied right now.
        if (!m_isApplyingProfile || operationId != m_operationId) {
            return;
        }

        if (!success) {
            qCWarning(lcService) << "Failed to initiate display switch (async) for:" << config.name;
            m_isWaitingForDisplay = false;
            m_displayTimeoutTimer->stop();
            m_stageIssues << tr("the display switch failed");
            finalizeApplication(hasFollowUpActions(config) ? ApplyStatus::PartialSuccess : ApplyStatus::Failed);
        }
    });
}

void WorkspaceService::switchDisplayDirectly(const QString& displayId) {
    if (displayId.isEmpty()) {
        return;
    }

    if (isSwitchInProgress()) {
        qCWarning(lcCore) << "Another switch is in progress, ignoring direct display request:" << displayId;
        emit errorOccurred(tr("A display or profile switch is already in progress."));
        return;
    }

    qCDebug(lcCore) << "Switching display directly:" << displayId;

    ++m_operationId;
    m_isSwitchingDisplay = true;
    m_windowRestoreTimer->stop();
    clearWindowRestoreUndo();
    rememberCurrentWindowLayout();

    m_displayManager->setDisplayModeAsync(displayId).then(this, [this, displayId](bool success) {
        m_isSwitchingDisplay = false;

        if (!success) {
            qCWarning(lcService) << "Direct display switch failed:" << displayId;
            emit errorOccurred(tr("Failed to switch display."));
            return;
        }

        // A direct switch has no profile to wait for, so the settle delay also covers the debounce
        // that precedes the system's own topology notification.
        scheduleWindowLayoutRestore();
    });
}

void WorkspaceService::onDisplayChanged() {
    if (!m_isWaitingForDisplay)
        return;

    QString currentKey = m_displayManager->getCurrentDisplayKey();

    if (currentKey == m_pendingConfig.displayId) {
        qCDebug(lcCore) << "Display switch confirmed for profile:" << m_pendingConfig.name;
        m_isWaitingForDisplay = false;
        m_displayChangeSucceeded = true;
        m_displayTimeoutTimer->stop();
        finalizeApplication(ApplyStatus::Success);
        scheduleWindowLayoutRestore();
    }
}

void WorkspaceService::onDisplayTimeout() {
    if (!m_isWaitingForDisplay)
        return;

    qCDebug(lcCore) << "Display switch timed out, applying audio anyway.";
    m_isWaitingForDisplay = false;
    m_stageIssues << tr("the display did not confirm the switch");
    finalizeApplication(ApplyStatus::PartialSuccess);
}

void WorkspaceService::finalizeApplication(ApplyStatus status) {
    const bool audioSwitchConfigured = !m_pendingConfig.audioId.isEmpty();
    const bool appLaunchConfigured = !m_pendingConfig.appsToLaunch.isEmpty();

    // Switch Default Audio Output synchronously during profile application.
    // Setting the default endpoint is extremely fast (< 5ms) and guarantees that:
    // 1. Any COM error is caught synchronously and degrades the status to PartialSuccess/Failed instantly.
    // 2. The confirmation beep is played STRICTLY on the newly applied audio device (resolves wrong-device beep bug).
    if (audioSwitchConfigured) {
        m_audioManager->setDefaultOutputDevice(m_pendingConfig.audioId);
    }

    if (m_hadAudioError) {
        m_stageIssues << tr("the audio device was not set");
        const bool hasKnownSuccessfulAction = m_displayChangeSucceeded;

        if (!hasKnownSuccessfulAction && !appLaunchConfigured) {
            status = ApplyStatus::Failed;
        } else if (status == ApplyStatus::Success) {
            status = ApplyStatus::PartialSuccess;
        }
    }

    // Closing the outgoing profile's applications only makes sense once the display really moved: a
    // failed or timed-out switch leaves the user on the old screen, where those windows belong. The
    // profile that is being applied again is not an outgoing one, so its windows stay untouched.
    const bool displayStepIsSettled = !m_displaySwitchRequested || m_displayChangeSucceeded;
    if (displayStepIsSettled && !m_outgoingProfileId.isEmpty() && m_outgoingProfileId != m_pendingConfig.id) {
        m_appLauncher->closeProfileProcesses(m_outgoingProfileId);
    }

    if (appLaunchConfigured) {
        if (!m_appLauncher->launchSequence(m_pendingConfig.id, m_pendingConfig.appsToLaunch)) {
            m_hadAppLaunchError = true;
        }
    }

    if (m_hadAppLaunchError) {
        m_stageIssues << tr("some applications did not start");
        const bool audioSwitchSucceeded = audioSwitchConfigured && !m_hadAudioError;
        const bool hasKnownSuccessfulAction = m_displayChangeSucceeded || audioSwitchSucceeded;

        if (!hasKnownSuccessfulAction) {
            status = ApplyStatus::Failed;
        } else if (status == ApplyStatus::Success) {
            status = ApplyStatus::PartialSuccess;
        }
    }

    // Optional audio feedback (Beep) - will now play strictly on the new default device!
    // A profile that did not achieve anything must not confirm itself: the beep is what the user
    // hears as "switched", and on a failed apply the screens are still where they were.
    if (m_audioConfirmation && m_hadActualChanges && status != ApplyStatus::Failed) {
        emit requestAudioFeedback();
    }

    qCDebug(lcCore) << "Profile application finished:" << m_pendingConfig.name
                    << ", status =" << applyStatusName(status) << ", issues =" << m_stageIssues;
    emit configApplyFinished(m_pendingConfig, status, m_stageIssues.join(u", "_s));

    m_isApplyingProfile = false;
    m_displayChangeSucceeded = false;
    m_hadAudioError = false;
    m_hadAppLaunchError = false;
    m_stageIssues.clear();
    m_hadActualChanges = false;
}

void WorkspaceService::rememberCurrentWindowLayout() {
    if (!m_rememberWindowLayouts || m_windowLayouts == nullptr) {
        return;
    }

    m_windowLayouts->remember(m_windowLayouts->topologyKey(), m_windowLayouts->capture());
}

void WorkspaceService::scheduleWindowLayoutRestore() {
    if (!m_rememberWindowLayouts || m_windowLayouts == nullptr) {
        return;
    }

    m_windowRestoreTimer->start(Utils::WindowLayoutRestoreDelayMs);
}

void WorkspaceService::restoreWindowLayout() {
    if (!m_rememberWindowLayouts || m_windowLayouts == nullptr) {
        return;
    }

    const QString topology = m_windowLayouts->topologyKey();
    const QList<Services::WindowPlacement> layout = m_windowLayouts->layoutFor(topology);
    if (layout.isEmpty()) {
        // Nothing has been visited from these screens yet; their layout is created when the user
        // leaves them.
        qCDebug(lcCore) << "No saved window layout for the current screen configuration.";
        return;
    }

    // Taken immediately before the move: this is what an undo has to bring back, rather than a
    // layout that was remembered on some earlier visit to these screens.
    const QList<Services::WindowPlacement> beforeRestore = m_windowLayouts->capture();
    const int placed = m_windowLayouts->restore(layout);
    qCDebug(lcCore) << "Window layout restored:" << placed << "of" << layout.size() << "windows placed.";

    if (placed <= 0) {
        return;
    }

    m_undoLayout = beforeRestore;
    m_undoExpiryTimer->start(m_undoTimeoutMs);
    emit windowRestoreUndoAvailable(true);
}

void WorkspaceService::clearWindowRestoreUndo() {
    m_undoExpiryTimer->stop();

    if (m_undoLayout.isEmpty()) {
        return;
    }

    m_undoLayout.clear();
    emit windowRestoreUndoAvailable(false);
}

QString WorkspaceService::applyStatusName(WorkspaceService::ApplyStatus status) {
    switch (status) {
    case WorkspaceService::ApplyStatus::Success:
        return u"success"_s;
    case WorkspaceService::ApplyStatus::PartialSuccess:
        return u"partial-success"_s;
    case WorkspaceService::ApplyStatus::Failed:
        return u"failed"_s;
    }

    return u"unknown"_s;
}

} // namespace ModeFlow::Core