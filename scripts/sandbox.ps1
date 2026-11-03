param(
    # Launch ModeFlow inside the sandbox as a freshly created standard user, so the shield
    # and the UAC consent dialog on the autostart checkbox are actually reachable.
    [switch]$StandardUser,

    # Force Admin Approval Mode for the built-in Administrator (FilterAdministratorToken=1).
    # The sandbox account is RID 500, which runs with a full token by default, so without
    # this the elevation path can never be exercised from the default account.
    [switch]$FilterAdmin,

    # Keep the sandbox off the network: the autostart identity check needs no connectivity.
    [switch]$NoNetwork,

    # Throwaway password for the accounts created inside the sandbox. It also lands in the
    # desktop helper scripts, because Start-Process -Credential cannot prompt for it.
    [string]$SandboxPassword = 'ModeFlow123!',

    [switch]$SkipBuild,

    # Generate everything and print it instead of starting the virtual machine.
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " ModeFlow - Windows Sandbox Runner (autostart identity)     " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# 1. Windows Sandbox has to be enabled; this check needs no administrator rights.
$sandboxExe = Join-Path $env:SystemRoot "System32\WindowsSandbox.exe"

if (-not (Test-Path -LiteralPath $sandboxExe)) {
    Write-Host "`n[ERROR] Windows Sandbox is not enabled or installed on this system!" -ForegroundColor Red
    Write-Host "To enable it:" -ForegroundColor Yellow
    Write-Host "  1. Run elevated PowerShell as Administrator:" -ForegroundColor White
    Write-Host "     Enable-WindowsOptionalFeature -FeatureName 'Containers-DisposableClientVM' -Online" -ForegroundColor White
    Write-Host "  2. Or open 'optionalfeatures.exe' -> check 'Windows Sandbox' -> reboot.`n" -ForegroundColor White
    exit 1
}

# 2. Fresh static Release build, one self-contained executable.
$projectRoot = (Resolve-Path "$PSScriptRoot\..").Path
$releaseDir = Join-Path $projectRoot "build\bin\Release"
$targetExe = Join-Path $releaseDir "ModeFlow.exe"

if (-not $SkipBuild) {
    Write-Host "`n[BUILD] Compiling a fresh static Release binary..." -ForegroundColor Cyan
    & cmd.exe /c "call `"$PSScriptRoot\release.cmd`""
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Release build failed! Aborting the sandbox launch." -ForegroundColor Red
        exit 1
    }
}

if (-not (Test-Path -LiteralPath $targetExe)) {
    if ($DryRun) {
        Write-Host "[WARN] $targetExe is missing, a dry run does not need it." -ForegroundColor Yellow
    }
    else {
        Write-Host "[ERROR] Release executable not found at: $targetExe" -ForegroundColor Red
        exit 1
    }
}

# 3. Staging. A mapped folder with write access is bidirectional, so nothing here points at
#    the repository: the sandbox gets a read-only copy of the build and writes its reports
#    to a separate folder under %TEMP%.
$stageRoot = Join-Path $env:TEMP "ModeFlowSandbox"
$stageDir = Join-Path $stageRoot "stage"
$outDir = Join-Path $stageRoot "out"

if (Test-Path -LiteralPath $stageDir) {
    try {
        Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction Stop
    }
    catch {
        # An open sandbox session keeps a handle on the mapped folder, so its contents
        # cannot be replaced underneath it.
        Write-Host "[ERROR] $stageDir is in use by an open Windows Sandbox session." -ForegroundColor Red
        Write-Host "Close the sandbox window and run this script again." -ForegroundColor Red
        exit 1
    }
}
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

# Leftover command files from a previous session would run again at the first poll.
foreach ($sub in 'inbox', 'outbox') {
    $subDir = Join-Path $outDir $sub
    if (Test-Path -LiteralPath $subDir) {
        Remove-Item -LiteralPath $subDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$setupScript = Join-Path $PSScriptRoot "sandbox-setup.ps1"
if (-not (Test-Path -LiteralPath $setupScript)) {
    Write-Host "[ERROR] $setupScript is missing, the sandbox would have nothing to run." -ForegroundColor Red
    exit 1
}

Copy-Item -LiteralPath $setupScript -Destination $stageDir -Force
$watchScript = Join-Path $PSScriptRoot "sandbox-watch.ps1"
if (Test-Path -LiteralPath $watchScript) {
    Copy-Item -LiteralPath $watchScript -Destination $stageDir -Force
}
if (Test-Path -LiteralPath $targetExe) {
    Copy-Item -LiteralPath $targetExe -Destination $stageDir -Force
}

# 4. The logon command is a single path, so the per-run options travel inside a generated
#    wrapper instead of the .wsb file.
$setupFlags = @()
if ($StandardUser) { $setupFlags += "-LaunchAsStandardUser" }
if ($FilterAdmin) { $setupFlags += "-FilterAdministratorToken" }

$logonCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\mf-host\sandbox-setup.ps1"'
$logonCommand += ' -SandboxPassword "' + $SandboxPassword + '"'
if ($setupFlags.Count -gt 0) {
    $logonCommand += " " + ($setupFlags -join " ")
}

$logonScript = Join-Path $stageDir "logon.cmd"
$logonBody = "@echo off`r`n"
$logonBody += 'start "ModeFlow sandbox watch" /min powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "C:\mf-host\sandbox-watch.ps1"' + "`r`n"
$logonBody += $logonCommand + "`r`n"
$logonBody += "pause`r`n"
Set-Content -LiteralPath $logonScript -Value $logonBody -Encoding ASCII

# 5. Sandbox configuration.
$networking = if ($NoNetwork) { "Disable" } else { "Default" }

$wsbContent = @"
<!-- Generated by scripts\sandbox.ps1 - do not edit, edit the script. -->
<Configuration>
  <VGpu>Default</VGpu>
  <Networking>$networking</Networking>
  <AudioInput>Default</AudioInput>
  <VideoInput>Disable</VideoInput>
  <ProtectedClient>Disable</ProtectedClient>
  <PrinterRedirection>Disable</PrinterRedirection>
  <ClipboardRedirection>Default</ClipboardRedirection>

  <MappedFolders>
    <MappedFolder>
      <HostFolder>$stageDir</HostFolder>
      <SandboxFolder>C:\mf-host</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$outDir</HostFolder>
      <SandboxFolder>C:\mf-out</SandboxFolder>
      <ReadOnly>false</ReadOnly>
    </MappedFolder>
  </MappedFolders>

  <LogonCommand>
    <Command>C:\mf-host\logon.cmd</Command>
  </LogonCommand>
</Configuration>
"@

$wsbPath = Join-Path $stageRoot "ModeFlow_Sandbox.wsb"
[System.IO.File]::WriteAllText($wsbPath, $wsbContent, [System.Text.Encoding]::UTF8)

Write-Host "`n[STAGE] $stageDir" -ForegroundColor Green
Get-ChildItem -LiteralPath $stageDir | ForEach-Object {
    Write-Host ("        {0}  {1} bytes" -f $_.Name, $_.Length)
}
Write-Host "[OUT]   $outDir  (reports survive closing the sandbox)" -ForegroundColor Green
Write-Host "[WSB]   $wsbPath" -ForegroundColor Green

if ($DryRun) {
    Write-Host "`n[DRY RUN] configuration that would be used:" -ForegroundColor Yellow
    Write-Host $wsbContent
    Write-Host "`n[DRY RUN] logon wrapper:" -ForegroundColor Yellow
    Get-Content -LiteralPath $logonScript | ForEach-Object { Write-Host "  $_" }
    exit 0
}

Write-Host "`n[SANDBOX] Launching Windows Sandbox..." -ForegroundColor Green
Start-Process -FilePath $wsbPath

Write-Host @"

Inside the sandbox:
  ModeFlow.exe is copied to C:\ModeFlow\ModeFlow.exe and launched by C:\mf-host\logon.cmd.
  Accounts created there (throwaway password, the sandbox dies on close):
    mfuser    - standard user, for the shield and consent path
    mfadmin2  - second administrator, to enter different credentials in the consent dialog
  Desktop shortcuts: run as mfuser / run as mfadmin2 / check autostart / redo setup.

  After toggling autostart, check whose task it became:
    schtasks /query /tn ModeFlow /v /fo LIST
  and compare it with the report in %TEMP%\ModeFlowSandbox\out on the host.

  ModeFlow only runs for one user at a time: the local server name is derived from the
  product name alone, while the activation secret is derived from USERNAME, so a second
  user's instance reaches the first user's server, is rejected and exits. Close the
  running instance before starting one under another account.
"@ -ForegroundColor White
