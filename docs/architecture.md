# ModeFlow — System Architecture & Design

This document describes the high-level software architecture, threading model, and critical system patterns implemented in the **ModeFlow** utility.

---

## 🏗️ 1. Composition Root & Service Lifecycle

To prevent tight coupling and decouple dependency creation from business logic, ModeFlow implements a strict **Composition Root** pattern using dedicated, stateless classes:

```
[AppController] ──> triggers ──> [ServiceFactory] (Instantiates AppServices)
       │
       └──────────> triggers ──> [ServiceWiring]  (Connects Signals & Slots)
```

1.  **`AppServices` (Struct):** A plain data container holding `std::unique_ptr` handles to all long-lived application, UI, hardware, and utility services.
2.  **`ServiceFactory`:** Natively instantiates services in three distinct phases:
    *   *Core Services:* Config, translation, and basic logic managers.
    *   *Hardware Services:* Display, Audio, Autostart, Hotkey, and Update managers.
    *   *UI Components:* Main window, dialogs, and tray icon controllers.
3.  **Strict RAII Ownership:** To prevent dangerous double-destruction vulnerabilities common in Qt (where a `QObject` parent deletes its children, but a smart pointer also owns them), all services managed by `std::unique_ptr` inside `AppServices` are created with `nullptr` as their `QObject` parent. They are owned **exclusively** by their smart pointers.
4.  **`ServiceWiring`:** Connects signals and slots across thread boundaries. By enforcing connections here, individual components remain 100% blind to each other’s concrete implementations.
    *   *Lazy-loading Connections:* To prevent `nullptr` connection errors on startup, connections involving the main window (`MainWindow`) are safely isolated inside `wireWindowConnections()`. This method is executed strictly once, right after the main window is instantiated on demand.

---

## 🧵 2. Threading Model & Async Boundaries

ModeFlow behaves as a lightweight, non-blocking background service. To prevent GUI stuttering (blocking the main thread during heavy OS driver queries), tasks are divided into distinct execution contexts:

### A. Main GUI Thread
*   Manages the event loop, draws UI components (`MainWindow`, `SettingsDialog`), and processes window frames (Mica, immersive dark mode).
*   Coordinates application state transitions.

### B. Concurrent Thread Pool (QThreadPool & QtConcurrent)
*   **Display Operations:** Calls to `QueryDisplayConfig` and `SetDisplayConfig` (within `DisplayManager::setDisplayModeAsync`) are executed concurrently via `QtConcurrent::run`. This isolates Windows display driver latency (which can freeze a thread for up to 2 seconds) away from the GUI.
*   **Autostart Modifications:** Writing logon tasks to the Windows Task Scheduler (via COM APIs) is done inside a `QtConcurrent::run` block.
*   **Asynchronous Autostart Checking:** Opening the Settings dialog is completely non-blocking because it queries the Task Scheduler status asynchronously on startup using `isAutostartEnabledAsync()` returning `QFuture`. The checkbox state is updated via the `.then()` continuation pattern once the background task completes.
*   **Manual Audio Overrides:** Changing default playback and communication devices via the system tray menu is offloaded to `QThreadPool::globalInstance()->start()`. A custom `ComInitGuard` handles thread-safe COM STA initialization on these background pool threads.

### C. Synchronous Hardware Coordination (Audio Switch & Beep)
*   While manual audio switches run asynchronously in `QThreadPool` to prevent GUI stutters, the profile-driven audio switch inside `WorkspaceService::finalizeApplication()` is invoked **synchronously** (`m_audioManager->setDefaultOutputDevice()`).
*   Since setting the default audio endpoint is extremely fast (< 5ms), synchronous execution ensures that:
    1.  The audio switch completes *before* the application launches.
    2.  Any COM error is caught synchronously and degrades the profile status instantly.
    3.  The confirmation beep (`requestAudioFeedback()`) is played **strictly on the newly applied audio device**, completely eliminating the audible bug of the beep playing on the old device.

---

## 💾 3. Unified State Management & Consolidated Persistence

Writing settings to disk/registry synchronously via `QSettings::sync()` can cause microscopic UI stutters. ModeFlow implements a **Consolidated Persistence Pattern** that completely removes disk I/O from normal runtime operations:

1.  **In-Memory Synchronization:** During the session, window sizes, positions, maximized states, and visibility are written instantly to `ConfigManager`'s memory. No physical disk writes are performed when the window is shown or hidden.
2.  **Safe closeEvent Handling:** When a user closes the window, it hides to the tray. To prevent Windows shutdown phases or programmatic exits from falsely overwriting the visibility state to `false` during the widget destruction phase, `MainWindow::closeEvent()` uses `event->spontaneous()`. It only writes `setMainWindowVisible(false)` when the close event is spontaneous (user clicked X or Alt+F4).
3.  **Single-Point Disk Flush:** The physical disk synchronization (`saveConfig()`) is connected to the global `QCoreApplication::aboutToQuit` signal. This guarantees that all settings, window sizes, and active profiles are successfully saved to the registry/INI file **exactly once** during application shutdowns, Windows restarts, or sign-outs.

---

## 🔒 4. Single-Instance Guard & Local IPC

To prevent multiple instances of the utility from running simultaneously, ModeFlow uses a custom local socket named pipe (`QLocalServer`/`QLocalSocket`) with an integrated security handshake:

```
[New Instance] ──(connects to Named Pipe)──> [Active Instance Server]
       │                                                 │
       ├─── Sends: "ACTIVATE:<SHA256 Token>" ────────────┤
       │                                                 ▼
     [Exits] <─── [Raises Window] <─── (Validates token matches current user)
```

1.  **Listen-or-Connect:** The app tries to connect to the unique named pipe.
2.  **Authorization Token:** To prevent unauthorized processes or malicious local scripts from triggering window popups, the active instance requires a **SHA-256 activation token** sent via the socket.
3.  **Token Generation:** The token is generated dynamically based on the current active Windows login name and a static, compiled application salt. Connections with missing or incorrect tokens are silently dropped.
4.  **Crashed Server Recovery:** If a connection fails but `listen` reports the address is in use, the guard assumes the previous instance crashed. It automatically purges the stale named pipe via `QLocalServer::removeServer()` and claims the socket safely.

---

## 🎨 5. UI/UX Component Separation (Passive Presenters)

To prevent `MainWindow` from becoming a bloated "God Object" managing layouts, database transactions, and custom controls, its responsibilities are strictly decoupled into independent, modular components:

```
                  [MainWindow] (View Coordinator)
                             │
      ┌──────────────────────┼──────────────────────┐
      ▼                      ▼                      ▼
[ProfileIconMenu]   [ProfileEditor]         [ProfileTransfer]
(Icon grid popup)   (Data mapping, capture) (Import/Export dialogs)
```

1.  **`ProfileEditor` (Presenter):** Encapsulates the entire right-side form. Handles name changes, suggested icon lookups, autostart configurations, and loading/saving profile structures.
2.  **`ProfileTransfer` (Presenter):** Isolates all file transactions. Uses custom, styled `QFileDialog` widgets (with native Mica glass backdrops, rounded corners, and disabled size-grip overlaps) to import/export JSON configuration files.
3.  **`ProfileIconMenu` (Popup QMenu):** Self-contained, grid-based icon picker. It handles its own layout, selections, and automatically repaints its child button icons when the active theme changes without any assistance from the main window.
4.  **`LogViewerDialog` (High-Performance Diagnostics):** Upgraded using a native, lazy-loaded `QSyntaxHighlighter` and `QStringView` parsing. It processes megabytes of logs instantly with 0% main-thread HTML freezing.
5.  **`AppLauncher` (Delayed Abort Protection):** Switched from untrackable `QTimer::singleShot` to managed, cancelable timers. If a user switches profiles before a delayed application launch completes, `terminateProfileProcesses` immediately stops and deletes the active timers, preventing "runaway" process launches.
6.  **Active Profile Visual Feedback:** To visually distinguish the active profile (real system state) from the selected profile (UI selection), `FluentListItemDelegate` queries `WorkspaceModel::ActiveRole` and draws a subtle 6px Fluent accent-colored dot on the left of the name.
7.  **DPI-Aware Adaptive Spacing:** In the `light.qss` and `dark.qss` stylesheets, group boxes are styled as modern, rounded, semi-transparent **Fluent Cards**. On style changes, dialogs dynamically invoke `adjustSize()` via `QLayout::SetMinimumSize` constraints to perfectly shrink or expand the layout without clipping.
```
