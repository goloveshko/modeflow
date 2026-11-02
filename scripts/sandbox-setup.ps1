# Runs INSIDE Windows Sandbox. The host launcher (scripts/sandbox.ps1) maps the staging
# folder to C:\mf-host (read-only) and the report folder to C:\mf-out (read-write, so the
# results survive closing the sandbox), then starts this script through LogonCommand.
#
# LogonCommand runs again after every logon inside the sandbox, so every step here is
# idempotent: an existing user is kept, an existing install is overwritten.
param(
    [string]$SourceDir = 'C:\mf-host',
    [string]$InstallDir = 'C:\ModeFlow',
    [string]$OutDir = 'C:\mf-out',
    [string]$StandardUser = 'mfuser',
    [string]$SecondAdmin = 'mfadmin2',
    [string]$SandboxPassword = 'ModeFlow123!',
    [switch]$FilterAdministratorToken,
    [switch]$LaunchAsStandardUser,
    [switch]$LaunchAsSecondAdmin,
    [switch]$ReportOnly
)

$ErrorActionPreference = 'Continue'
$script:LogFile = Join-Path $OutDir ('setup-{0:yyyyMMdd-HHmmss}.log' -f (Get-Date))

# Mirrors APP_COMPANY_NAME / APP_PRODUCT_NAME in src/utils/VersionInfo.h: the app registers its
# task under QCoreApplication::applicationName(), which is the product display name.
$script:Company = 'Goloveshko'
$script:ProductName = 'ModeFlow - Workspace Manager'

function Write-Log {
    param([string]$Message)
    $line = '[{0:HH:mm:ss}] {1}' -f (Get-Date), $Message
    Write-Host $line
    if (-not (Test-Path -LiteralPath $OutDir)) {
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    }
    Add-Content -LiteralPath $script:LogFile -Value $line -Encoding UTF8
}

function Test-SandboxUser {
    param([string]$Name)
    & net user $Name 2>&1 | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function New-SandboxUser {
    param([string]$Name, [switch]$Administrator)

    if (Test-SandboxUser $Name) {
        Write-Log "user $Name already exists, keeping it"
    }
    else {
        & net user $Name $SandboxPassword /add 2>&1 | Out-Null
        Write-Log "created user $Name (net user exit=$LASTEXITCODE)"
    }

    if ($Administrator) {
        # The group alias is localized on a non-English guest, so address it by SID:
        # 'net localgroup Administrators' fails there with error 1376.
        $sid = 'S-1-5-32-544'
        $member = Get-LocalGroupMember -SID $sid -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "*\$Name" }
        if ($member) {
            Write-Log "$Name is already in Administrators"
        }
        else {
            Add-LocalGroupMember -SID $sid -Member $Name
            Write-Log ("added {0} to Administrators ({1})" -f $Name, $sid)
        }
    }
}

function Install-ModeFlow {
    $source = Join-Path $SourceDir 'ModeFlow.exe'
    if (-not (Test-Path -LiteralPath $source)) {
        Write-Log "ERROR: $source is missing, the host launcher did not stage a build"
        return $null
    }

    if (-not (Test-Path -LiteralPath $InstallDir)) {
        New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    }

    # The task stores the executable path it was given, so ModeFlow has to run from a real
    # sandbox path: a mapped folder is a host folder seen from inside, and the registered
    # path would be meaningless outside this session.
    # A running instance keeps its own image locked, so the previous one has to be stopped
    # before the new build can be copied over it; without this a second logon silently
    # keeps the old binary and reports it as installed.
    Stop-ModeFlow

    $target = Join-Path $InstallDir 'ModeFlow.exe'
    try {
        Copy-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop
    }
    catch {
        Write-Log ("ERROR: cannot replace {0}: {1}" -f $target, $_.Exception.Message)
        return $null
    }

    Write-Log ("installed {0} ({1} bytes)" -f $target, (Get-Item -LiteralPath $target).Length)
    return $target
}

function Set-AdminTokenFilter {
    $key = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
    $current = (Get-ItemProperty -Path $key -Name FilterAdministratorToken -ErrorAction SilentlyContinue).FilterAdministratorToken
    if ($current -eq 1) {
        Write-Log 'FilterAdministratorToken is already 1'
        return
    }

    Set-ItemProperty -Path $key -Name FilterAdministratorToken -Value 1 -Type DWord
    Write-Log 'FilterAdministratorToken set to 1: the built-in Administrator now gets a filtered token'
    Write-Log 'restart the sandbox session (shutdown /r /t 0 inside it) for the policy to apply'
}

function Stop-ModeFlow {
    $running = @(Get-Process -Name ModeFlow -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) {
        return
    }

    # Ask the windows to close first: ModeFlow flushes its configuration on aboutToQuit,
    # so a hard kill would drop the settings the next step is supposed to compare against.
    foreach ($process in $running) {
        $null = $process.CloseMainWindow()
    }
    Start-Sleep -Seconds 2

    $left = @(Get-Process -Name ModeFlow -ErrorAction SilentlyContinue)
    foreach ($process in $left) {
        Write-Log ("ModeFlow pid={0} ignored the close request, terminating" -f $process.Id)
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    Write-Log ("stopped {0} ModeFlow instance(s)" -f $running.Count)
}

function Start-ModeFlowAsUser {
    param([string]$Name, [string]$ExePath)

    $secure = ConvertTo-SecureString $SandboxPassword -AsPlainText -Force
    $credential = New-Object System.Management.Automation.PSCredential("$env:COMPUTERNAME\$Name", $secure)
    Start-Process -FilePath $ExePath -WorkingDirectory $InstallDir -Credential $credential

    Write-Log "launched $ExePath as $Name"
    Write-Log 'expect a filtered (medium integrity) token: the shield on the autostart checkbox'
    Write-Log 'and on Save, then the consent dialog when the checkbox is toggled'
}

function Write-DesktopHelpers {
    param([string]$SetupScript)

    $desktop = [Environment]::GetFolderPath('Desktop')
    # The helpers carry the password the run actually used: the accounts were created with
    # it, and Start-Process -Credential has no way to ask for it interactively.
    $powershell = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -SandboxPassword "{1}"' -f $SetupScript, $SandboxPassword

    $helpers = [ordered]@{
        'ModeFlow - run as mfuser.cmd'      = "$powershell -LaunchAsStandardUser"
        'ModeFlow - run as mfadmin2.cmd'    = "$powershell -LaunchAsSecondAdmin"
        'ModeFlow - check autostart.cmd'    = "$powershell -ReportOnly"
        'ModeFlow - redo setup.cmd'         = $powershell
    }

    foreach ($name in $helpers.Keys) {
        $path = Join-Path $desktop $name
        $body = "@echo off`r`n" + $helpers[$name] + "`r`npause`r`n"
        Set-Content -LiteralPath $path -Value $body -Encoding ASCII
    }
    Write-Log ("wrote {0} helper script(s) to the desktop" -f $helpers.Count)
}

# The task is registered under QCoreApplication::applicationName(), which is the product display
# name and not "ModeFlow", so it is located through its action target instead of a guessed name.
function Find-ModeFlowTask {
    $byAction = @(Get-ScheduledTask -ErrorAction SilentlyContinue |
        Where-Object { $_.Actions | Where-Object { $_.Execute -like '*ModeFlow.exe' } })
    if ($byAction.Count -gt 0) {
        return $byAction[0]
    }
    return Get-ScheduledTask -TaskName $script:ProductName -ErrorAction SilentlyContinue
}

function Invoke-Report {
    $reportPath = Join-Path $OutDir ('report-{0:yyyyMMdd-HHmmss}.txt' -f (Get-Date))
    $lines = New-Object System.Collections.Generic.List[string]

    $add = {
        param([string]$Text)
        $lines.Add($Text)
        Write-Host $Text
    }

    $section = {
        param([string]$Title, [scriptblock]$Body)
        & $add ''
        & $add ('== ' + $Title + ' ==')
        $output = & $Body 2>&1 | Out-String
        foreach ($line in ($output -split "`r?`n")) {
            & $add $line
        }
    }

    & $add ('ModeFlow autostart identity report, {0:yyyy-MM-dd HH:mm:ss}' -f (Get-Date))

    & $section 'who is running this' {
        whoami
        whoami /groups | Select-String -Pattern 'Mandatory Level|S-1-5-32-544|S-1-16-'
    }

    & $section 'UAC policy' {
        $key = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
        foreach ($name in 'EnableUAC', 'FilterAdministratorToken', 'ConsentPromptBehaviorAdmin') {
            $value = (Get-ItemProperty -Path $key -Name $name -ErrorAction SilentlyContinue).$name
            '{0} = {1}' -f $name, $value
        }
    }

    & $section 'local accounts' {
        net user
        Get-LocalGroupMember -SID 'S-1-5-32-544' -ErrorAction SilentlyContinue |
            Format-Table Name, ObjectClass, PrincipalSource -AutoSize
    }

    $task = Find-ModeFlowTask

    & $section 'every task that launches ModeFlow.exe' {
        $all = @(Get-ScheduledTask -ErrorAction SilentlyContinue |
            Where-Object { $_.Actions | Where-Object { $_.Execute -like '*ModeFlow.exe' } })
        if ($all.Count -eq 0) {
            'none'
        }
        foreach ($item in $all) {
            '{0}{1}  state={2}' -f $item.TaskPath, $item.TaskName, $item.State
        }
    }

    & $section 'registered task (schtasks)' {
        if (-not $task) {
            'no task registered by ModeFlow'
        }
        else {
            'task name: {0}{1}' -f $task.TaskPath, $task.TaskName
            schtasks /query /tn $task.TaskName /v /fo LIST
        }
    }

    & $section 'registered task (ScheduledTasks module)' {
        if (-not $task) {
            'no task registered by ModeFlow'
        }
        else {
            'Author: ' + $task.Author
            $task.Principal | Format-List UserId, LogonType, RunLevel, GroupId
            $task.Actions | Format-List Execute, Arguments, WorkingDirectory
            $task.Triggers | Format-List CimClass, UserId, Enabled, Delay
            'State: ' + $task.State
        }
    }

    & $section 'task file owner and ACL' {
        $taskFile = Join-Path $env:SystemRoot ('System32\Tasks\' + $(if ($task) { $task.TaskName } else { $script:ProductName }))
        if (Test-Path -LiteralPath $taskFile) {
            (Get-Acl -LiteralPath $taskFile).Owner
            icacls $taskFile
        }
        else {
            "$taskFile does not exist"
        }
    }

    & $section 'running instances and their owners' {
        $processes = @(Get-WmiObject Win32_Process -Filter "Name='ModeFlow.exe'" -ErrorAction SilentlyContinue)
        if ($processes.Count -eq 0) {
            'ModeFlow.exe is not running'
        }
        foreach ($process in $processes) {
            $owner = $process.GetOwner()
            'pid={0} owner={1}\{2} path={3}' -f $process.ProcessId, $owner.Domain, $owner.User, $process.ExecutablePath
        }
    }

    # QSettings uses the native format, so per-user state is a registry key in every loaded hive.
    & $section 'per-user configuration' {
        foreach ($hive in (Get-ChildItem 'Registry::HKEY_USERS' -ErrorAction SilentlyContinue)) {
            if ($hive.PSChildName -like '*_Classes') {
                continue
            }
            $account = $hive.PSChildName
            try {
                $account = (New-Object System.Security.Principal.SecurityIdentifier($hive.PSChildName)).
                    Translate([System.Security.Principal.NTAccount]).Value
            } catch {
            }
            $key = 'Registry::HKEY_USERS\' + $hive.PSChildName + '\Software\' + $script:Company + '\' + $script:ProductName
            if (-not (Test-Path -LiteralPath $key)) {
                continue
            }
            '{0} ({1})' -f $account, $hive.PSChildName
            $settings = Get-Item -LiteralPath $key
            foreach ($name in $settings.GetValueNames()) {
                '  {0} = {1}' -f $(if ($name) { $name } else { '(default)' }), $settings.GetValue($name)
            }
            foreach ($sub in (Get-ChildItem -LiteralPath $key -ErrorAction SilentlyContinue)) {
                '  [subkey] {0}' -f $sub.PSChildName
            }
        }
        $log = Join-Path $InstallDir 'log.txt'
        if (Test-Path -LiteralPath $log) {
            '{0}  {1} bytes' -f $log, (Get-Item -LiteralPath $log).Length
        }
    }

    if (-not (Test-Path -LiteralPath $OutDir)) {
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    }
    Set-Content -LiteralPath $reportPath -Value $lines -Encoding UTF8
    Write-Host ''
    Write-Host ('report written to ' + $reportPath)
    Write-Host 'on the host it is %TEMP%\ModeFlowSandbox\out'
}

if ($ReportOnly) {
    Invoke-Report
    return
}

Write-Log 'ModeFlow sandbox setup started'

$exePath = Install-ModeFlow
New-SandboxUser -Name $StandardUser
New-SandboxUser -Name $SecondAdmin -Administrator

if ($FilterAdministratorToken) {
    Set-AdminTokenFilter
}

$setupScript = Join-Path $SourceDir 'sandbox-setup.ps1'
if (Test-Path -LiteralPath $setupScript) {
    Write-DesktopHelpers -SetupScript $setupScript
}

Write-Log "standard user : $StandardUser / $SandboxPassword"
Write-Log "second admin  : $SecondAdmin / $SandboxPassword (same password, in Administrators)"

if ($exePath -and $LaunchAsStandardUser) {
    Start-ModeFlowAsUser -Name $StandardUser -ExePath $exePath
}
elseif ($exePath -and $LaunchAsSecondAdmin) {
    Start-ModeFlowAsUser -Name $SecondAdmin -ExePath $exePath
}
elseif (-not $exePath -and ($LaunchAsStandardUser -or $LaunchAsSecondAdmin)) {
    Write-Log 'launch skipped: the install step above failed, nothing new was copied'
}

Invoke-Report
Write-Log 'setup finished'
