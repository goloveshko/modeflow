# Guest-side command channel for the Windows Sandbox session. Windows Sandbox offers no
# remote exec, so the host drops *.ps1 files into the read-write mapped folder and this
# loop runs them, writing the console output next to them. The host deletes the inbox
# file once it has read the log: deleting or renaming inside a mapped folder from within
# the container can hang the share filter, so the guest never mutates the folder beyond
# creating new files. Each child is bounded so one stuck step cannot kill the loop.
param(
    [string]$Inbox = 'C:\mf-out\inbox',
    [string]$Outbox = 'C:\mf-out\outbox',
    [int]$PollSeconds = 2,
    [int]$StepTimeoutMs = 180000
)

$ErrorActionPreference = 'Continue'
$processed = @{}

foreach ($dir in $Inbox, $Outbox) {
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

while ($true) {
    $pending = @(Get-ChildItem -LiteralPath $Inbox -Filter '*.ps1' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime)

    foreach ($file in $pending) {
        if ($processed.ContainsKey($file.Name)) {
            continue
        }
        $processed[$file.Name] = $true

        $log = Join-Path $Outbox ($file.BaseName + '.log')
        $err = Join-Path $Outbox ($file.BaseName + '.err')
        $child = Start-Process -FilePath powershell.exe -WindowStyle Hidden -PassThru `
            -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $file.FullName `
            -RedirectStandardOutput $log -RedirectStandardError $err

        if (-not $child.WaitForExit($StepTimeoutMs)) {
            $null = $child.Kill()
            Add-Content -LiteralPath $log -Value ('TIMED OUT after {0} ms and was killed' -f $StepTimeoutMs) -Encoding UTF8
        }
        Add-Content -LiteralPath $log -Value ('exit=' + $child.ExitCode) -Encoding UTF8
    }

    Start-Sleep -Seconds $PollSeconds
}
