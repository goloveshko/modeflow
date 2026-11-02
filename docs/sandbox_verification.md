# ModeFlow — Windows Sandbox Verification Guide

A few behaviours of ModeFlow cannot be checked from the account you are typing in: the UAC shield
on the *Start with Windows* switch, the consent dialog behind it, and who actually ends up owning
the Task Scheduler logon task the app registers. Logging out of the real desktop to watch them is
slow, and a unit test cannot ask Windows whose task a registration produced. `scripts\sandbox.ps1`
starts a disposable Windows Sandbox session that answers exactly these questions.

---

## 🧯 1. What makes the session safe to use

* Windows Sandbox is a separate, ephemeral virtual machine: closing its window discards every
  account, task, file and setting created inside it.
* The staged build is mapped **read-only** (`C:\mf-host`), so a session can never write back into
  the repository or into your build tree.
* Only the report folder is mapped writable (`C:\mf-out`), and it lives under
  `%TEMP%\ModeFlowSandbox\out` on the host, which is why a result survives closing the window.
* These scripts never touch the host Task Scheduler, the host registry, or a ModeFlow that is
  already running on your desktop.

---

## ✅ 2. Requirements

* Windows 10 or 11 Pro/Enterprise with the optional feature enabled (needs one elevated run and a
  reboot; the launcher only checks and tells you, it does not enable it for you):
  ```powershell
  Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM
  ```
* A static release executable, because the guest receives one self-contained file:
  ```bash
  scripts\build.bat --release --static --ninja
  ```
  `sandbox.ps1` rebuilds it by itself unless you pass `-SkipBuild`; `sandbox.cmd` is the wrapper
  that always skips the rebuild and uses `build\bin\Release\ModeFlow.exe` as it stands.

---

## ▶️ 3. Running a check

```cmd
scripts\sandbox.cmd                    :: reuse the current release exe
scripts\sandbox.ps1                    :: rebuild first, then start the session
```

| Switch | Effect |
| --- | --- |
| `-StandardUser` | Launches ModeFlow inside the guest as the standard account `mfuser`, so the shield and the consent dialog are reachable. |
| `-FilterAdmin` | Sets `FilterAdministratorToken=1` in the guest, which gives the built-in Administrator (RID 500) a filtered token; without it the default sandbox account can never trigger elevation. The policy applies after the next session restart. |
| `-NoNetwork` | Disables networking in the guest; an identity check needs no connectivity. |
| `-SkipBuild` | Use the existing release exe instead of compiling one. |
| `-DryRun` | Stage everything, print the `.wsb` configuration and the logon wrapper, and do not start the virtual machine. |
| `-SandboxPassword` | Throwaway password for the guest accounts, `ModeFlow123!` unless you pass another one — the host launcher and the guest setup script default to the same value, so it is also what you type in the consent dialog. It exists only inside the VM and is written into the desktop helpers, because `Start-Process -Credential` cannot prompt for it. |

The launcher writes `%TEMP%\ModeFlowSandbox\ModeFlow_Sandbox.wsb` and starts it. If the staging
folder is reported as *in use*, another sandbox window still holds it — close that session and run
again.

---

## 🖥️ 4. Inside the session

Setup runs automatically at logon and is idempotent, so a second logon reuses the accounts and
replaces the executable:

* `mfuser` — standard user, for the shield and the consent path.
* `mfadmin2` — second administrator, for entering different credentials in the consent dialog.
* ModeFlow installs to `C:\ModeFlow\ModeFlow.exe`, never from a mapped folder, because the logon
  task stores the path it was given and a mapped path means nothing outside the session.
* Four helper scripts land on the desktop: run as `mfuser`, run as `mfadmin2`, re-collect the
  report, redo setup.

Every run writes `setup-*.log` and `report-*.txt` to the report folder. The report answers the
question the check was about:

* whose token the current process runs under (`whoami /groups`, mandatory level, membership);
* which account the registered task belongs to, through the ScheduledTasks module, `schtasks` and
  the owner and ACL of the file under `C:\Windows\System32\Tasks`;
* which user holds which `Software\Goloveshko\ModeFlow - Workspace Manager` configuration, read
  from every loaded hive.

To see the task from the guest side while the window is open:

```cmd
schtasks /query /tn "ModeFlow - Workspace Manager" /v /fo LIST
```

---

## 📡 5. Command channel

Windows Sandbox offers no remote execution, so `sandbox-watch.ps1` runs hidden inside the guest and
polls `C:\mf-out\inbox`. Drop a `.ps1` into `%TEMP%\ModeFlowSandbox\out\inbox` on the host and it
is executed there; its console output appears as `outbox\<name>.log` (stderr as `.err`, exit code
appended, a step that outlives its timeout is killed and marked). Delete the inbox copy from the
host once you have read the log — the guest only ever creates files, because deleting or renaming
inside a mapped folder from within the container can hang the share filter.

---

## ⚠️ 6. Caveats

* **One user at a time.** ModeFlow derives its local server name from the product name alone while
  the activation secret comes from `USERNAME`, so an instance started under a second account
  reaches the first one's server, is rejected and exits. Close the running instance before
  starting under another account.
* `FilterAdministratorToken` is a logon-time policy: set it, then restart the guest session.
* Everything except `%TEMP%\ModeFlowSandbox\out` is lost when the window closes, and a session
  that was killed by the host simply forgets the task you were about to inspect — re-run the
  desktop helper instead of trying to recover it.
* A sandbox result is evidence about Windows behaviour, not a substitute for the test suite; the
  unit tests still have to pass before anything is committed.
