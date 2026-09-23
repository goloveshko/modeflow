#pragma once

#include <QStringView>

/**
 * @brief Global constants for the ModeFlow application.
 * Contains central timeouts, delays, limits, and magic numbers.
 */

namespace ModeFlow::Utils {

// ============================================================================
// Timeouts and Delays (in milliseconds)
// ============================================================================

/** Local socket connection timeout (ms) */
constexpr int LocalSocketTimeoutMs = 500;
static_assert(LocalSocketTimeoutMs >= 100 && LocalSocketTimeoutMs <= 5000,
              "LocalSocketTimeoutMs must be between 100ms and 5s");

/** System readiness poll interval at startup (ms) */
constexpr int SystemReadyPollIntervalMs = 500;
static_assert(SystemReadyPollIntervalMs >= 10 && SystemReadyPollIntervalMs <= 500,
              "SystemReadyPollIntervalMs must be between 10ms and 500ms");

/** Maximum number of system readiness poll attempts */
constexpr int SystemReadyMaxAttempts = 20;
static_assert(SystemReadyMaxAttempts >= 10 && SystemReadyMaxAttempts <= 10000,
              "SystemReadyMaxAttempts must be between 10 and 10000");

/** Default display switch timeout (ms) */
constexpr int DisplaySwitchTimeoutMs = 10000;
static_assert(DisplaySwitchTimeoutMs >= 1000 && DisplaySwitchTimeoutMs <= 60000,
              "DisplaySwitchTimeoutMs must be between 1s and 60s");

/** Debounce interval for display events (ms) */
constexpr int DisplayDebounceIntervalMs = 500;
static_assert(DisplayDebounceIntervalMs >= 100 && DisplayDebounceIntervalMs <= 5000,
              "DisplayDebounceIntervalMs must be between 100ms and 5s");

/** Tray notification duration (ms) */
constexpr int TrayNotificationDurationMs = 3000;
static_assert(TrayNotificationDurationMs >= 1000 && TrayNotificationDurationMs <= 30000,
              "TrayNotificationDurationMs must be between 1s and 30s");

/** Maximum time to wait for the elevated helper process (ms) */
constexpr int ElevatedHelperTimeoutMs = 30000;
static_assert(ElevatedHelperTimeoutMs >= 1000 && ElevatedHelperTimeoutMs <= 120000,
              "ElevatedHelperTimeoutMs must be between 1s and 120s");

/** Silent application restart delay on startup logon display switch (ms) */
constexpr int StartupSilentRestartDelayMs = 1200;
static_assert(StartupSilentRestartDelayMs >= 100 && StartupSilentRestartDelayMs <= 10000,
              "StartupSilentRestartDelayMs must be between 100ms and 10s");

/** Debounce delay for profile auto-saving (ms) */
constexpr int ProfileAutosaveDebounceMs = 350;

// ============================================================================
// Sizes and Limits
// ============================================================================

/** Maximum log file size before rotation (bytes) — 5 MB */
constexpr int MaxLogFileSizeBytes = 5 * 1024 * 1024;

/** Maximum application launch delay in profile (seconds) — 5 minutes */
constexpr int MaxAppLaunchDelaySeconds = 300;

/** Default fallback Qt style key */
inline constexpr QStringView DefaultQtStyleKey = u"windows11";

// ============================================================================
// Auto-Update
// ============================================================================

/** Minimum interval between automatic update checks (ms) — 24 hours */
constexpr int UpdateCheckIntervalMs = 24 * 3600 * 1000;

/** Network transfer timeout for update check requests (ms) */
constexpr int UpdateTransferTimeoutMs = 15000;
static_assert(UpdateTransferTimeoutMs >= 1000 && UpdateTransferTimeoutMs <= 60000,
              "UpdateTransferTimeoutMs must be between 1s and 60s");

} // namespace ModeFlow::Utils