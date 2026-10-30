#include "TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>

#include "ComInitGuard.h"
#include "Constants.h"
#include "Logging.h"

#ifdef Q_OS_WIN
#include <comdef.h>
#include <sddl.h>
#include <taskschd.h>
#include <winsvc.h>
#include <wrl/client.h>
#endif

using Microsoft::WRL::ComPtr;

namespace ModeFlow::Utils {

using namespace Qt::StringLiterals;

// Helper to convert HRESULT to string for logging
#ifdef Q_OS_WIN
static QString hresultToString(HRESULT hr) {
    _com_error err(hr);
    return QString::fromWCharArray(err.ErrorMessage());
}
#endif

namespace {
void fillDefaults(QString& taskName, QString& exePath) {
    if (taskName.isEmpty()) {
        taskName = QCoreApplication::applicationName();
        if (taskName.isEmpty()) {
            taskName = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
        }
    }
    if (exePath.isEmpty()) {
        exePath = QCoreApplication::applicationFilePath();
    }
}

// A dropped HRESULT leaves a task registered without the settings the caller believes it has.
[[nodiscard]] bool require(HRESULT hr, const char* step) {
    if (FAILED(hr)) {
        qCWarning(lcUtil) << step << "failed:" << hresultToString(hr);
        return false;
    }
    return true;
}

// Task Scheduler is a COM service, so the service, folder and task handles are acquired
// through one path that reports which step refused.
bool connectTaskService(ComPtr<ITaskService>& pService, ComPtr<ITaskFolder>& pRootFolder) {
    if (!require(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pService)),
                 "CoCreateInstance(TaskScheduler)")) {
        return false;
    }

    if (!require(pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()), "Connect")) {
        return false;
    }

    return require(pService->GetFolder(_bstr_t(L"\\"), &pRootFolder), "GetFolder");
}
} // namespace

bool TaskScheduler::createTaskAtLogon(const QString& arguments, int delaySeconds, bool runHighest,
                                      const QString& principalSid, QString taskName, QString exePath) {
    if (!isAdmin()) {
        qCWarning(lcUtil) << "Administrator rights required";
        return false;
    }

    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        qCWarning(lcUtil) << "Cannot access Task Scheduler service";
        return false;
    }
    CloseServiceHandle(hSCM);

    fillDefaults(taskName, exePath);

    ComInitGuard comGuard(ComInitGuard::Apartment::MultiThreaded);
    if (!comGuard.isOk()) {
        qCWarning(lcUtil) << "COM is unavailable, the autostart task was left untouched";
        return false;
    }

    ComPtr<ITaskService> pService;
    ComPtr<ITaskFolder> pRootFolder;
    ComPtr<ITaskDefinition> pTask;
    ComPtr<ITaskSettings> pSettings;
    ComPtr<ITriggerCollection> pTriggerCollection;
    ComPtr<ITrigger> pTrigger;
    ComPtr<ILogonTrigger> pLogonTrigger;
    ComPtr<IActionCollection> pActionCollection;
    ComPtr<IAction> pAction;
    ComPtr<IExecAction> pExecAction;
    ComPtr<IPrincipal> pPrincipal;
    ComPtr<IRegisteredTask> pRegisteredTask;

    if (!connectTaskService(pService, pRootFolder)) {
        return false;
    }

    // Registration uses TASK_CREATE_OR_UPDATE, so an existing task is replaced only once the
    // new definition is complete. Deleting it first would leave no autostart on any later failure.
    if (!require(pService->NewTask(0, &pTask), "NewTask")) {
        return false;
    }

    if (!require(pTask->get_Principal(&pPrincipal), "get_Principal") ||
        !require(pPrincipal->put_RunLevel(runHighest ? TASK_RUNLEVEL_HIGHEST : TASK_RUNLEVEL_LUA), "put_RunLevel")) {
        return false;
    }

    // Without an explicit user the scheduler pins the task to the registering process, so a task
    // created by an elevated helper would belong to the admin instead of the account that asked.
    if (!principalSid.isEmpty()) {
        if (!isValidUserSid(principalSid)) {
            qCWarning(lcUtil) << "Refused to register the task for a SID that is not a user account:" << principalSid;
            return false;
        }
        if (!require(pPrincipal->put_UserId(_bstr_t(principalSid.toStdWString().c_str())), "put_UserId")) {
            return false;
        }
    }

    if (!require(pTask->get_Settings(&pSettings), "get_Settings") ||
        !require(pSettings->put_StartWhenAvailable(VARIANT_TRUE), "put_StartWhenAvailable") ||
        !require(pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE), "put_DisallowStartIfOnBatteries") ||
        !require(pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE), "put_StopIfGoingOnBatteries") ||
        !require(pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")), "put_ExecutionTimeLimit") ||
        !require(pSettings->put_Priority(5), "put_Priority")) {
        return false;
    }

    if (!require(pTask->get_Triggers(&pTriggerCollection), "get_Triggers") ||
        !require(pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger), "Create(logon trigger)") ||
        !require(pTrigger.As(&pLogonTrigger), "as ILogonTrigger") ||
        !require(pLogonTrigger->put_Id(_bstr_t(L"LogonTrigger")), "put_Id")) {
        return false;
    }

    // A delay is an ISO 8601 duration (PT#S); the settings UI only offers up to 30 seconds.
    if (delaySeconds > 0) {
        const int clampedDelay = std::clamp(delaySeconds, 1, 30);
        const QString delayStr = u"PT%1S"_s.arg(clampedDelay);
        if (!require(pLogonTrigger->put_Delay(_bstr_t(delayStr.toStdWString().c_str())), "put_Delay")) {
            return false;
        }
        qCDebug(lcUtil) << "Setting logon delay to" << clampedDelay << "seconds.";
    } else if (!require(pLogonTrigger->put_Delay(nullptr), "put_Delay")) {
        return false;
    }

    if (!require(pTask->get_Actions(&pActionCollection), "get_Actions") ||
        !require(pActionCollection->Create(TASK_ACTION_EXEC, &pAction), "Create(exec action)") ||
        !require(pAction.As(&pExecAction), "as IExecAction")) {
        return false;
    }

    if (!require(pExecAction->put_Path(_bstr_t(QDir::toNativeSeparators(exePath).toStdWString().c_str())),
                 "put_Path") ||
        !require(pExecAction->put_Arguments(_bstr_t(arguments.toStdWString().c_str())), "put_Arguments")) {
        return false;
    }

    const QString workingDir = QFileInfo(exePath).absolutePath();
    if (!require(
            pExecAction->put_WorkingDirectory(_bstr_t(QDir::toNativeSeparators(workingDir).toStdWString().c_str())),
            "put_WorkingDirectory")) {
        return false;
    }

    if (!require(pRootFolder->RegisterTaskDefinition(_bstr_t(taskName.toStdWString().c_str()), pTask.Get(),
                                                     TASK_CREATE_OR_UPDATE, _variant_t(), _variant_t(),
                                                     TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(L""), &pRegisteredTask),
                 "RegisterTaskDefinition")) {
        return false;
    }

    // Updating a task keeps the descriptor it was created with and ignores the one passed to
    // RegisterTaskDefinition, so read access for the new principal is set on the task itself.
    if (const QString sddl = taskSecurityDescriptor(principalSid); !sddl.isEmpty()) {
        if (!require(pRegisteredTask->SetSecurityDescriptor(_bstr_t(sddl.toStdWString().c_str()), 0),
                     "SetSecurityDescriptor")) {
            return false;
        }
    }

    qCDebug(lcUtil) << "Task created successfully:" << taskName;
    return true;
}

bool TaskScheduler::removeTask(QString taskName) {
    QString exePath;
    fillDefaults(taskName, exePath);

    ComInitGuard comGuard(ComInitGuard::Apartment::MultiThreaded);
    if (!comGuard.isOk()) {
        qCWarning(lcUtil) << "COM is unavailable, the autostart task was left untouched";
        return false;
    }

    ComPtr<ITaskService> pService;
    ComPtr<ITaskFolder> pRootFolder;

    if (!connectTaskService(pService, pRootFolder)) {
        return false;
    }

    const HRESULT hr = pRootFolder->DeleteTask(_bstr_t(taskName.toStdWString().c_str()), 0);
    if (FAILED(hr)) {
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
            qCDebug(lcUtil) << "Task already absent:" << taskName;
            return true;
        }
        qCWarning(lcUtil) << "DeleteTask failed:" << hresultToString(hr);
        return false;
    }

    qCDebug(lcUtil) << "Task removed successfully:" << taskName;
    return true;
}

bool TaskScheduler::isTaskRegistered(QString taskName, QString exePath) {
    fillDefaults(taskName, exePath);

    ComInitGuard comGuard(ComInitGuard::Apartment::MultiThreaded);
    if (!comGuard.isOk()) {
        qCWarning(lcUtil) << "COM is unavailable, the autostart state could not be read";
        return false;
    }

    ComPtr<ITaskService> pService;
    ComPtr<ITaskFolder> pRootFolder;
    ComPtr<IRegisteredTask> pRegisteredTask;

    if (!connectTaskService(pService, pRootFolder)) {
        return false;
    }

    const HRESULT found = pRootFolder->GetTask(_bstr_t(taskName.toStdWString().c_str()), &pRegisteredTask);
    if (FAILED(found)) {
        // A missing task is a real answer; anything else means the state could not be read.
        if (found != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
            qCWarning(lcUtil) << "GetTask failed:" << hresultToString(found);
        }
        return false;
    }

    ComPtr<ITaskDefinition> pDefinition;
    if (!require(pRegisteredTask->get_Definition(&pDefinition), "get_Definition")) {
        return false;
    }

    ComPtr<IActionCollection> pActions;
    if (!require(pDefinition->get_Actions(&pActions), "get_Actions")) {
        return false;
    }

    // Task Scheduler uses 1-based indexing for actions.
    ComPtr<IAction> pAction;
    if (!require(pActions->get_Item(1, &pAction), "get_Item")) {
        return false;
    }

    ComPtr<IExecAction> pExecAction;
    if (!require(pAction.As(&pExecAction), "as IExecAction")) {
        return false;
    }

    BSTR rawPath = nullptr;
    if (!require(pExecAction->get_Path(&rawPath), "get_Path")) {
        return false;
    }

    _bstr_t pathWrapper(rawPath, false);
    const QString registeredPath = QString::fromWCharArray(static_cast<const wchar_t*>(pathWrapper));

    return QString::compare(QDir::toNativeSeparators(exePath), QDir::toNativeSeparators(registeredPath),
                            Qt::CaseInsensitive) == 0;
}

bool TaskScheduler::isAdmin() {
    BOOL fRet = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            fRet = elevation.TokenIsElevated;
        }
    }
    if (hToken)
        CloseHandle(hToken);
    return fRet;
}

QString TaskScheduler::currentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        qCWarning(lcUtil) << "OpenProcessToken failed while reading the current user SID";
        return {};
    }

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);

    QString sid;
    if (needed > 0) {
        QByteArray buffer(static_cast<int>(needed), 0);
        if (GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
            const auto* info = reinterpret_cast<const TOKEN_USER*>(buffer.constData());
            LPWSTR text = nullptr;
            if (ConvertSidToStringSidW(info->User.Sid, &text)) {
                sid = QString::fromWCharArray(text);
                LocalFree(text);
            }
        }
    }

    CloseHandle(token);
    return sid;
}

bool TaskScheduler::isValidUserSid(const QString& sid) {
    PSID psid = nullptr;
    if (!ConvertStringSidToSidW(reinterpret_cast<LPCWSTR>(sid.utf16()), &psid)) {
        return false;
    }

    WCHAR account[64] = {};
    WCHAR domain[64] = {};
    DWORD accountSize = 64;
    DWORD domainSize = 64;
    SID_NAME_USE use = SidTypeUnknown;
    const BOOL lookedUp = LookupAccountSidW(nullptr, psid, account, &accountSize, domain, &domainSize, &use);
    LocalFree(psid);

    return lookedUp != FALSE && use == SidTypeUser;
}

QString TaskScheduler::taskSecurityDescriptor(const QString& principalSid) {
    if (principalSid.isEmpty()) {
        return {};
    }

    // Administrators and SYSTEM keep what the scheduler grants them by default, otherwise the
    // elevated helper could no longer update or remove the task it has just registered.
    return u"D:(A;ID;0x1f019f;;;BA)(A;ID;0x1f019f;;;SY)(A;ID;FA;;;BA)(A;;FR;;;%1)"_s.arg(principalSid);
}

bool TaskScheduler::runAsAdmin(const QString& argument) {
    QString path = QCoreApplication::applicationFilePath();
    std::wstring wPath = QDir::toNativeSeparators(path).toStdWString();
    std::wstring wArgs = argument.toStdWString();

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = wPath.c_str();
    sei.lpParameters = wArgs.c_str();
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExW(&sei)) {
        // If the user clicked "No", GetLastError will return ERROR_CANCELLED (1223)
        return false;
    }

    if (!sei.hProcess) {
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(sei.hProcess, ElevatedHelperTimeoutMs);
    if (waitResult != WAIT_OBJECT_0) {
        if (waitResult == WAIT_TIMEOUT) {
            qCWarning(lcUtil) << "Elevated helper timed out after" << ElevatedHelperTimeoutMs << "ms";
        } else {
            qCWarning(lcUtil) << "Failed while waiting for elevated helper. waitResult =" << waitResult;
        }
        CloseHandle(sei.hProcess);
        return false;
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(sei.hProcess, &exitCode)) {
        qCWarning(lcUtil) << "Failed to read elevated helper exit code. Error =" << GetLastError();
        CloseHandle(sei.hProcess);
        return false;
    }

    CloseHandle(sei.hProcess);

    if (exitCode != 0) {
        qCWarning(lcUtil) << "Elevated helper exited with code" << exitCode;
        return false;
    }

    return true;
}

} // namespace ModeFlow::Utils
