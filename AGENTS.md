# Project Instructions for AI Agents (ModeFlow)

## Build & Packaging

To build the project, run:
```bash
scripts\build.bat --debug --ninja
```

To build a static release package (ZIP + SHA-256 checksum + update.json):
```bash
scripts\build.bat --release --static --ninja --package
```

Output will be in `build\artifacts\`:
- `ModeFlow-vX.Y.Z-win-x64.zip`
- `ModeFlow-vX.Y.Z-win-x64.zip.sha256`
- `release_notes.md`

## Running Tests

Tests are automatically built and run as part of the build script. To run manually:
```bash
build\bin\Debug\ModeFlowTests.exe
```

## Project Overview

**ModeFlow** is a Qt6 C++20 Windows desktop utility for switching workspaces (monitors, audio, apps) via global hotkeys or a system tray menu. It runs as a highly optimized, non-blocking background service with full Windows 11 Fluent Design integration (Mica backdrop, rounded corners).

## Architecture

```
src/
  core/           — Application logic, settings, service wiring, composition root
    AppController   — Coordinator of the application lifecycle (starts/restarts/exits)
    ServiceFactory  — Composition Root (instantiates all core, hardware, and UI services)
    ServiceWiring   — Connects signals and slots between services (handles lazy-loading window connections)
    CliParser       — Parses CLI arguments into structured StartupOptions (C++17 Structured Binding)
    CommandLineBuilder — Fluent Builder Pattern for safe, unified command-line argument generation
    ConfigManager   — Thread-safe settings repository (mutable QMutex, double-buffered snapshot)
    WorkspaceModel  — Profile list model (exposes ActiveRole to track live applied profile)
    WorkspaceService — Coordinates workspace configuration (display switch -> settle delay -> audio & apps)
  gui/            — Qt dialogs, widgets, and MVC controllers
    MainWindow  — Main window (pure View, decoupled from data mapping and file dialogs)
    ProfileEditor — Presenter managing the right-side profile forms, autocomplete, and hardware capture
    ProfileTransfer — Presenter managing non-blocking QFileDialog import/export transactions
    ProfileIconMenu  — Custom grid-based QMenu widget for selecting profile icons (self-managing theme changes)
    SettingsDialog   — Application settings (DPI-aware, auto-adjusts size to fit Fluent Cards)
    AboutDialog      — Version info and silent update checker (contains centered OK close button)
    UpdateDialog     — Displays Markdown changelogs and provides update actions
    LogViewerDialog  — Local diagnostics console (lazy-loaded QSyntaxHighlighter, QScrollArea, and OK button)
    LogHighlighter   — High-performance QSyntaxHighlighter (zero-allocation parsing, theme-adaptive palette)
    FluentListItemDelegate — Custom sidebar item delegate
    TrayController   — Compact system tray icon and dynamic context menus
  services/       — Hardware/system services (mostly asynchronous or isolated)
    DisplayManager   — Asynchronous monitor switching (QueryDisplayConfig/SetDisplayConfig) with WinLuid types
    AudioDeviceManager — Audio output switching (integrates FredEmmott::Audio using Pimpl pattern)
    HotkeyManager    — Global hotkey registration (QHotkey, implements smart differential registration)
    UpdateService    — Lightweight auto-update checker (caches update_cache.json in AppData)
    StyleManager     — Theme management (Mica, acrylic, immersive dark/light mode, styled QFileDialog wrappers)
  utils/          — Utilities and constants
    ComInitGuard     — RAII COM initialization guard (split into .h/.cpp to isolate <windows.h> pollution)
    DeviceUtils      — Common sorting/grouping of devices (active top, inactive bottom)
    Constants.h      — All magic numbers, intervals, and URLs
    VersionInfo.h    — App version macros and product info
    WinKeyTranslator — Custom QTranslator (translates Meta to Win for shortcuts)
```

## Code Conventions

- **Namespaces**: `ModeFlow::Core`, `ModeFlow::Gui`, `ModeFlow::Services`, `ModeFlow::Utils`
- **Strings**: Use `u"..."_s` or `u"..."_sv` (Qt6 string literals) with `using namespace Qt::StringLiterals` for zero-allocation string handling.
- **Logging**: Use `qCDebug(lcCore)`, `qCWarning(lcService)`, etc. (categories in `Logging.h`). Never use empty function names `[]` inside lambda connections; delegate logging to named class methods instead.
- **String View Parsing**: Avoid heap allocations in performance-critical code. Use `std::string_view` or `QStringView` for string parsing (e.g., in `LogManager` and `LogViewerDialog::parseLine`).
- **Settings**: All persistent state goes through `ConfigManager` -> `QSettings`. 

## Mandatory Rules for AI Agents

### 1. Asynchronous UI & Non-blocking Operations
* **Never** call `QThread::sleep()` or `QThread::msleep()` on the main GUI thread.
* **Never** run heavy blocking Win32 APIs (like `SetDisplayConfig`) on the main GUI thread.
* Wrap all blocking operations in `QtConcurrent::run()` or `QThreadPool::globalInstance()->start()` and return a `QFuture<T>`.
* Use Qt6's `.then(this, [this](T result) { ... })` asynchronous continuations to update UI elements safely and sequentially.

### 2. Separation of Concerns in UI & Services
* **Pure Views (Decoupling):** `MainWindow` is a pure presenter. It must never contain business logic (like duplicating configurations, generating default names, or managing file dialogs). These operations must be delegated to `IWorkspaceManager` or dedicated controllers (`ProfileDetailsController`, `ProfileExchangeController`).
* **No Direct QFileDialog Calls:** Never call static `QFileDialog` methods inside UI views. Always route file dialog transactions through `m_styleManager->getOpenFileName()` or `getSaveFileName()`. This ensures that file dialogs are rendered as beautiful, rounded, Mica-glass Fluent frames matching the active theme, and prevents noisy Windows COM thread exceptions.
* **Data Provider Sorting:** UI elements (comboboxes and tray menus) should never sort or filter raw device lists themselves. `WorkspaceManagerImpl::getAvailableDisplays()` and `getAvailableAudioOutputs()` must return lists that are already sorted and grouped (active first) via `Utils::DeviceUtils::sortAndGroupDevices()`.
* **Dynamic Menu Generation:** The system tray menus (`TrayController`) must be populated dynamically on `aboutToShow()` to prevent stale states.
* **Clutter Filtering:** In the system tray submenus, completely filter out (break on) disconnected hardware devices to keep the context menu compact and native-looking.

### 3. Audio & Automation Lifecycle Safety
* **Synchronous Audio Finalization:** `WorkspaceService` must invoke `AudioDeviceManager::setDefaultOutputDevice()` synchronously inside `finalizeApplication()`. Because changing the default audio endpoint takes < 5ms, synchronous finalization guarantees that:
  * Any COM error is caught synchronously and degrades the profile apply status instantly.
  * The confirmation beep is played **strictly on the newly applied audio device**, completely eliminating the "beep on wrong device" audible bug.
* **Delayed Launch Abort Protection:** `AppLauncher` must never use untrackable `QTimer::singleShot` for delayed process launching. It must instantiate and track `QTimer` objects dynamically. When `terminateProfileProcesses()` is called, all pending/scheduled delayed launch timers for that profile must be stopped and deleted immediately to prevent runaway process launches.
* **Smart Hotkey Diffing:** `HotkeyManager` must never perform a "nuclear" unregistration of all global shortcuts upon every profile list change. It must implement a differential algorithm: compare new configurations with the previous, keep unchanged `QHotkey` instances completely untouched in the OS, register only new hotkeys, and unregister only deleted ones.

### 4. Window Geometry & Visibility State
* **No Inline QSS Styles:** All styling, colors, card layouts, margins, and hover effects must be declared strictly in `.qss` resource files. Use `#SettingsDialog QPushButton#btnSave` and similar object names.
* **Consolidated Disk Writes:** To prevent redundant disk I/O and micro-stutters during normal show/hide window toggles, do NOT call `saveSettings()` inside `showEvent` and `hideEvent`. Update the states in `ConfigManager` memory, and flush all configuration changes to disk exactly once during explicit application shutdown (`QCoreApplication::aboutToQuit`).
* **Active Profile Visual Feedback:** The active, applied profile must be visually highlighted in the sidebar list. `NavigationDelegate` must query `WorkspaceModel::ActiveRole` and draw a subtle 6px Fluent accent-colored dot on the left side of the name (between the icon and text). The text and icon of the active row must also use the active theme accent.
* **Safe closeEvent Handling:** To prevent Windows shutdown phases or programmatic exits (`qApp->quit()`) from falsely overwriting the visibility state to `false` inside `hideEvent()`, use `event->spontaneous()` inside `MainWindow::closeEvent()`. Only write `setMainWindowVisible(false)` when the event is spontaneous (user clicked X or pressed Alt+F4).

## Adding a New Service

1. Create `src/services/NewService.h/.cpp`
2. Add to `COMMON_SOURCES` in `CMakeLists.txt`
3. Add `std::unique_ptr<NewService>` to `AppServices` struct inside `ServiceFactory.h`
4. Instantiate in `ServiceFactory::createHardwareServices()` (or `createCoreServices()`)
5. Connect signals in `ServiceWiring::wireServiceConnections()`

## i18n Workflow

All user-facing strings must be wrapped in `tr()`. Translations live in `i18n/ModeFlow_ru_RU.ts`.

After adding/changing `tr()` strings:
1. Run `lupdate` to extract new translatable strings into the `.ts` file:
   ```bash
   scripts\build_lupdate.bat
   ```
2. Open `i18n/ModeFlow_ru_RU.ts` in Qt Linguist and translate the new entries.
3. Run `lrelease` to compile translations into `.qm` binary:
   ```bash
   scripts\build_lrelease.bat
   ```

## Git Staging Rules

* **Only stage files that are part of the project.** When using `git add`, add only the specific source files the agent created or modified for the current task. Never use `git add .` or `git add -A` — this can accidentally stage build artifacts, IDE configs, temporary files, or other unrelated content.
* Before staging, verify each file is a legitimate project source file that belongs in the repository.

## Commit Workflow

Commit after completing each logical task — one feature, one fix, one refactor. Use Conventional Commits standard: `<type>(<scope>): <description>` (e.g., `feat(ui):`, `fix(core):`, `refactor(audio):`).

Always verify the build and run tests before committing:
```bash
scripts\build.bat --debug --ninja
```
