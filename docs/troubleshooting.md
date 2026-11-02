# ModeFlow — Troubleshooting Guide

This guide helps resolve common system-level, display, audio, and permission issues when running or developing **ModeFlow** on Windows 10 or 11.

---

## 🚫 1. Drag & Drop of `.exe` Files is Blocked

### Symptom:
When trying to drag an executable file from Windows Explorer into the "Automation" list, the mouse cursor shows a "forbidden" sign, or nothing happens.

### Root Cause:
This is a native security feature of the Windows kernel called **UIPI (User Interface Privilege Isolation)**. 
* If ModeFlow was launched with elevated privileges (as Administrator, e.g., via the Task Scheduler logon task), Windows strictly blocks drag-and-drop operations coming from standard-user processes (like `explorer.exe`).

### Solution:
* **For Testing:** Run `ModeFlow.exe` as a standard, non-elevated user. Drag & drop will work immediately.
* **Alternative:** Instead of dragging, right-click on the "Automation" list and select **"Add program..."** to select the executable manually via our styled Fluent file explorer.

---

## ⌨️ 2. Global Hotkey Fails to Register

### Symptom:
A warning appears stating that a global shortcut could not be registered, or pressing the shortcut does nothing.

### Root Cause:
Windows only allows one application to register a specific global key combination at a time. If the shortcut is already bound by:
* Windows system shortcuts (e.g., `Win + L`, `Ctrl + Alt + Del`).
* Overlay utilities (GeForce Experience, Steam, Discord overlay).
* PowerToys or customized keyboard managers.

ModeFlow will fail to register the hotkey to prevent system-level conflicts.

### Solution:
1. Open the **Profile Settings** or **Application Settings** and bind a different, unique shortcut.
2. We recommend using distinct modifiers such as `Ctrl + Alt + [Key]` or `Ctrl + Shift + [Key]` to avoid overlapping with default OS shortcuts.

---

## 📺 3. Display or Audio Switching Fails (Partial Success / Failed Badges)

### Symptom:
Applying a profile results in a "Partial Success" or "Failed" warning badge in the main window or tray notification.

### Root Cause:
* **Displays:** The monitor configured in the profile is physically disconnected (HDMI/DP cable unplugged, or the monitor is powered down).
* **Audio:** The default audio device was unplugged (e.g. USB headset disconnected), causing our synchronous COM audio switch to safely intercept and report the failure.

### Solution:
1. Right-click the system tray icon, select **"Monitor"** or **"Profiles"**, and verify which monitors and audio devices are currently online and detected by the OS.
2. Open the main window, select your profile, click on the **Display/Audio comboboxes**, and select active devices. If your saved device is offline, it will be marked as `[Disconnected]` and styled grey.
3. Open the **Log Viewer** (`btnMore` -> "View Log") to inspect the Win32 error code returned by `SetDisplayConfig` or the COM `HRESULT` error logged by `AudioDeviceManager::setDefaultOutputDevice`.

---

## ⏳ 4. Delayed Application Launch is Cancelled

### Symptom:
When switching from Profile A (which has a program scheduled to launch with a 10-second delay) to Profile B after 3 seconds, the program from Profile A never launches.

### Root Cause:
This is a **safe automation feature** of ModeFlow. 
* To prevent "runaway" processes from launching on top of your newly applied workspace, ModeFlow uses managed, cancelable timers. 
* Switching away from a profile instantly aborts all pending delayed application launches for that profile.

---

## 🔬 5. Diagnostics: Logs & Crash Dumps

If you encounter unexpected application behavior or crashes, use these built-in diagnostic tools to trace the issue.

### Analyzing the Log File:
ModeFlow writes dynamic diagnostic logs to a local file.
* **Paths:** 
  * `log.txt` located directly in the application folder (if write-accessible).
  * `%TEMP%\log.txt` (if the application directory is write-protected, e.g. inside `Program Files`).
* **UI Tool:** Click the `⋮` (More) button on the main toolbar and select **"View Log"** to open our custom real-time log inspector with built-in level, category, and search filters.

### Analyzing Crash Dumps:
If the application crashes, a native Windows minidump (`.dmp`) is written to disk via `CrashHandler`.
* **Path:** `%APPDATA%\ModeFlow\CrashDumps\`
* **How to debug:**
  1. Open Visual Studio.
  2. Drag and drop the `.dmp` file into the editor.
  3. Click **"Debug with Native Only"** on the right panel.
  4. Visual Studio will load the symbols and point you directly to the exact line of C++ code where the failure occurred.
