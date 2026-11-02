# Contributing to ModeFlow

Thank you for your interest in contributing to **ModeFlow**! ModeFlow is a lightweight, high-performance Windows utility. We welcome contributions from developers of all experience levels.

Please take a moment to review this document to ensure your contribution matches our code quality and architectural standards.

---

## 🛠️ Setup & Development Prerequisites

### System Requirements:
*   **Operating System:** Windows 10 or 11 (x64) [3.5].
*   **Build Toolchain:** Visual Studio 2022 (MSVC v143), CMake 3.20+, and Ninja [3.5].
*   **Framework:** Qt 6.5+ (Widgets, Concurrent, Svg, Test, Network) [3.5].

### Environment Tools:
We provide a pre-configured `.clangd` language server file and a `.clang-format` configuration in the repository root. These tools will automatically provide you with IDE-grade code completion, diagnostics, and formatting inside **VS Code** (using the `clangd` extension), **Zed**, **CLion**, or **Visual Studio**.

Please run the formatter before making a commit:
```bash
scripts\build.bat --format
```

---

### 📦 Setting Up Static Qt 6 via vcpkg

ModeFlow is compiled as a static, self-contained binary on Windows to eliminate DLL dependencies, optimize memory consumption, and simplify portable distribution [3.5]. We use **vcpkg** to manage and compile our static Qt 6 dependency [3.5].

#### Step 1: Install vcpkg
If you do not have `vcpkg` installed globally, clone the repository and run the bootstrap script:
```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

#### Step 2: Compile Static Qt 6 Packages
Run the following verified command to compile and install all the necessary Qt 6 libraries, tools, and translations statically. This compiles the exact minimal subset of Qt 6 required by ModeFlow:
```bash
.\vcpkg install "qtbase[openssl]" qttools qtsvg qttranslations --triplet=x64-windows-static
```
*Note: Compiling Qt from source using vcpkg is a heavy task and can take anywhere from 30 minutes to 2 hours depending on your CPU.*

#### Step 3: Integrate vcpkg with CMake
When configuring the ModeFlow project in your IDE (Visual Studio 2022, CLion, Zed, or command-line CMake), you must specify the `vcpkg` toolchain file and target the static triplet:
```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

#### Step 4: Updating the Static Toolchain
To upgrade your static Qt 6 libraries and tools to their latest upstream versions:
1. Open the directory where `vcpkg` is installed.
2. Pull the latest upstream package definitions (ports):
   ```bash
   git pull
   .\vcpkg update
   ```
3. Run the non-interactive upgrade command to rebuild the static packages:
   ```bash
   .\vcpkg upgrade --no-dry-run --triplet=x64-windows-static
   ```

---

## 🧩 Architectural Guidelines

When adding services or editing application logic, respect our core design principles:

1.  **Non-blocking UI:** Never block the main GUI thread. Heavy Win32 APIs (e.g., `SetDisplayConfig`) or sleep loops must be moved to background threads via `QThreadPool::globalInstance()->start()` or `QtConcurrent::run` and sequentially applied to the UI using `QFuture::then()` continuations [1, 2].
2.  **Thread-Safe Settings:** `ConfigManager` is thread-safe and utilizes in-memory configurations. All getters and setters must be protected by a `QMutexLocker locker(&m_mutex)` using a `mutable QMutex` [1]. Configuration changes are flushed to disk exactly once during explicit application shutdown (`QCoreApplication::aboutToQuit`).
3.  **Zero Inline Styles:** Never write hardcoded stylesheet strings in C++ code (do not call `widget->setStyleSheet(...)`) [5.3]. Move all margins, colors, borders, and hover-reveal effects into `.qss` resource files (`common.qss`, `dark.qss`, `light.qss`) and use precise `objectName` selectors [5.3].
4.  **No Direct QFileDialog Calls:** To keep our visual consistency on Windows 11, never call static `QFileDialog` methods. Use the `getOpenFileName()` and `getSaveFileName()` wrappers provided by `IStyleManager`. These guarantee that the file explorer is compiled with native Mica glass backdrops, immersive dark borders, and matches the active `.qss` stylesheet perfectly.
5.  **Memory Management:** Use raw pointers and native Qt parent-child trees (`new QTimer(this)`) for owned UI children or timers [3.6]. Avoid `std::unique_ptr` for parent-owned `QObject` elements to prevent double-destruction crashes [3.6].
6.  **Language and Comments:** All source code comments, variable names, and documentation must be written in **English**. User-facing strings must be wrapped in `tr()` for proper i18n support.

---

## 🧪 Testing Requirements

When adding a feature or fixing a bug, please write corresponding tests inside `tests/ModeFlowTests.cpp`. 

*   Do not submit PRs with failing tests.
*   Run the test suite manually before committing:
    ```bash
    build\bin\Debug\ModeFlowTests.exe
    ```
*   Behaviour that depends on UAC — the shield on the autostart switch, the consent dialog behind
    it, and which account the registered logon task ends up belonging to — cannot be exercised from
    your own account and cannot be asserted from a unit test. Check those in a disposable Windows
    Sandbox session instead: see [docs/sandbox_verification.md](docs/sandbox_verification.md).

---

## 📝 Commit Messages & Branches

We follow the **Conventional Commits** standard. Ensure your commit messages use one of the following prefixes:

*   `feat(<scope>):` for new features (e.g., `feat(ui): add compact list spacing`).
*   `fix(<scope>):` for bug fixes (e.g., `fix(tray): fix icon theme synchronization`).
*   `refactor(<scope>):` for code cleanups (e.g., `refactor(core): decouple list widget`).
*   `docs(<scope>):` for updates to markdown guides.

Create a new branch from `main` for your feature or fix:
```bash
git checkout -b feature/your-feature-name
```
Submit your pull request (PR) targeting the `main` branch.

---

## 🔒 Security Vulnerabilities
If you discover a security vulnerability (such as a local IPC privilege escalation or input validation bypass), please do not open a public issue. Instead, contact the maintainer directly.
