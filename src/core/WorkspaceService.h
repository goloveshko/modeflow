#pragma once

#include <QObject>
#include <QStringList>

#include "ConfigTypes.h"

namespace ModeFlow::Services {
class AudioDeviceManager;
class AppLauncher;
class DisplayManager;
} // namespace ModeFlow::Services

namespace ModeFlow::Core {

/**
 * @brief Service for managing workspace configuration application.
 *
 * WorkspaceService coordinates the application of workspace configurations,
 * handling the complex sequence of display switching, audio device changes,
 * and optional application launching.
 *
 * Key responsibilities:
 * - Display switching with visual confirmation (waits for displaysChanged signal)
 * - Audio device switching after display change completes
 * - Application launching with configurable delay
 * - Display switch timeout handling (falls back to audio-only if timeout expires)
 * - Reports whether the profile fully succeeded or finished with warnings
 */
class WorkspaceService : public QObject {
    Q_OBJECT
public:
    enum class ApplyStatus { Success, PartialSuccess, Failed };
    Q_ENUM(ApplyStatus)

    explicit WorkspaceService(Services::DisplayManager* displayManager, Services::AudioDeviceManager* audioManager,
                              Services::AppLauncher* appLauncher, QObject* parent = nullptr);

    ~WorkspaceService() = default;

    void applyWorkspaceConfig(const WorkspaceConfig& config);

    /**
     * @brief Switch the display without applying a profile.
     *
     * Used by the tray submenu. The result is reported to the user, and the request is refused
     * while another switch owns the display topology.
     */
    void switchDisplayDirectly(const QString& displayId);

    bool isWaitingForDisplay() const { return m_isWaitingForDisplay; }

    // A profile apply and a direct display switch both move the monitors, so only one of them may
    // be in flight; otherwise the reported result would belong to whichever callback arrived last.
    bool isSwitchInProgress() const { return m_isApplyingProfile || m_isSwitchingDisplay; }

    /**
     * @brief Set display switch timeout delay.
     * @param ms Timeout in milliseconds.
     *
     * If the display switch doesn't complete within this time, the service
     * will proceed with audio/app changes anyway.
     */
    void setDisplayTimeout(int ms);

    /**
     * @brief Enable/disable audio confirmation (beep on profile switch).
     * @param enabled true to enable audio feedback, false to disable.
     */
    void setAudioConfirmation(bool enabled);

    static QString applyStatusName(WorkspaceService::ApplyStatus status);

signals:
    void configApplyStarted(const WorkspaceConfig& config);

    /**
     * @brief One profile application ended.
     * @param issues Stages that did not work, phrased so they read after "problems:"; empty on
     *        success. The result alone would leave the user guessing which half of the profile
     *        failed, and the services report their own errors before this summary arrives.
     */
    void configApplyFinished(const WorkspaceConfig& config, ApplyStatus status, const QString& issues);
    void errorOccurred(const QString& message);

    /**
     * @brief Request to play audio feedback (confirmation sound).
     *
     * Connected to AudioFeedbackService::playConfirmation.
     */
    void requestAudioFeedback();

private slots:
    void onDisplayChanged();
    void onDisplayTimeout();

private:
    void finalizeApplication(ApplyStatus status);

private:
    Services::DisplayManager* m_displayManager;
    Services::AudioDeviceManager* m_audioManager;
    Services::AppLauncher* m_appLauncher;

    std::unique_ptr<QTimer> m_displayTimeoutTimer;
    WorkspaceConfig m_pendingConfig;

    int m_displayTimeoutMs;
    bool m_isWaitingForDisplay = false;
    bool m_isApplyingProfile = false;
    bool m_isSwitchingDisplay = false;
    bool m_displaySwitchRequested = false;
    // The profile the user is coming from, so its scheduled launches and applications can be handled
    // once this switch reaches a conclusion.
    QString m_outgoingProfileId;
    // Identifies the operation that is currently allowed to report a result, so a completion that
    // arrives after its own operation already ended is ignored instead of finishing the next one.
    quint64 m_operationId = 0;
    bool m_displayChangeSucceeded = false;
    bool m_hadAudioError = false;
    bool m_hadAppLaunchError = false;
    QStringList m_stageIssues;
    bool m_audioConfirmation = true;
    bool m_hadActualChanges = false;
};
} // namespace ModeFlow::Core
