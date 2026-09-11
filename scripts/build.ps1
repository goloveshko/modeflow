param(
    [Alias("c")][switch]$Clean,
    [Alias("s")][switch]$Static,
    [Alias("bq", "build-qt")][switch]$BuildQt,
    [Alias("n")][switch]$Ninja,
    [Alias("d")][switch]$Debug,
    [Alias("r")][switch]$Release,
    [Alias("t", "tests")][switch]$Test,
    [Alias("lu")][switch]$Lupdate,
    [Alias("lr")][switch]$Lrelease,
    [Alias("no")][switch]$NoObsolete,
    [Alias("k")][switch]$KillRunning,
    [Alias("p")][switch]$Package,
    [Alias("f")][switch]$Format,
    [Alias("dp")][switch]$Deploy,
    [Alias("h")][switch]$Help,
    [string]$QtDir
)

$ErrorActionPreference = "Stop"

# Show help if requested or if no action flags were provided at all
$HasAction = $Clean -or $Static -or $BuildQt -or $Ninja -or $Debug -or $Release -or $Test -or $Lupdate -or $Lrelease -or $KillRunning -or $Package -or $Format -or $Deploy -or $Help

if ($Help -or -not $HasAction) {
    Write-Host @"
ModeFlow Master Build & Packaging Automation Script

Usage: build.bat [OPTIONS]

  -c,  --clean         Clean build directory before building
  -s,  --static        Build with vcpkg (static linking)
  -bq, --build-qt      Build/install Qt static components via vcpkg
  -n,  --ninja         Use Ninja generator (default)
  -d,  --debug         Build Debug configuration
  -r,  --release       Build Release configuration
  -t,  --tests         Build unit tests target
  -lu, --lupdate       Run ONLY lupdate (extract strings to .ts)
  -lr, --lrelease      Run ONLY lrelease (compile .ts to .qm)
  -no, --noobsolete    Run lupdate only, removing obsolete translations
  -k,  --kill-running  Terminate ModeFlow before build if running
  -p,  --package       Create ZIP package and SHA-256 checksum
  -f,  --format        Run clang-format on src/ folder
  -dp, --deploy        Run windeployqt for shared builds (auto-detects shared binary)
  -h,  --help          Show this help message

Convenience wrappers:
  build_debug.bat     <== build.bat --debug --ninja
  build_release.bat   <== build.bat --release --static --ninja
  build_package.bat   <== build.bat --release --static --ninja --package
  run_tests.bat       <== build.bat --tests --debug --ninja
  build_lrelease.bat  <== build.bat --lrelease
  build_lupdate.bat   <== build.bat --lupdate
"@ -ForegroundColor Cyan
    exit 0
}

$Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# ─────────────────────────────────────────────────────────────
# Helper Functions
# ─────────────────────────────────────────────────────────────
function Write-Step([string]$Message) {
    Write-Host "`n[STEP] $Message" -ForegroundColor Cyan
}
function Write-Success([string]$Message) {
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}
function Write-Warn([string]$Message) {
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}
function Write-Err([string]$Message) {
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

# Fail-safe SHA-256 Checksum Calculator
function Get-Sha256Checksum([string]$FilePath) {
    if (Get-Command "Get-FileHash" -ErrorAction SilentlyContinue) {
        return (Get-FileHash -Path $FilePath -Algorithm SHA256).Hash.ToLower()
    }

    $Stream = [System.IO.File]::OpenRead($FilePath)
    try {
        $Sha256 = [System.Security.Cryptography.SHA256]::Create()
        $Bytes = $Sha256.ComputeHash($Stream)
        return ([System.BitConverter]::ToString($Bytes)).Replace("-", "").ToLower()
    } finally {
        $Stream.Close()
        $Stream.Dispose()
    }
}

# ─────────────────────────────────────────────────────────────
# 1. Environment Overrides & Path Setup
# ─────────────────────────────────────────────────────────────
$RootDir = Resolve-Path "$PSScriptRoot\.."
Set-Location $RootDir

# Load local environment overrides from environment.ps1
$EnvPs1 = Join-Path $PSScriptRoot "environment.ps1"
if (Test-Path $EnvPs1) {
    Write-Host "[ENV] Loading overrides from $EnvPs1" -ForegroundColor Gray
    . $EnvPs1
}

# Production Defaults (if not set in environment)
if (-not $env:GITHUB_URL) {
    $env:GITHUB_URL = "https://github.com/goloveshko/ModeFlow"
}
if (-not $env:UPDATE_URL) {
    $env:UPDATE_URL = "https://api.github.com/repos/goloveshko/ModeFlow/releases/latest"
}
if (-not $env:LICENSE_URL) {
    $env:LICENSE_URL = "https://github.com/goloveshko/ModeFlow/blob/main/LICENSE"
}

# Application & Folder Names
$AppNameBase = "ModeFlow"
$AppName = "$AppNameBase.exe"
$AppNameTests = "${AppNameBase}Tests.exe"
$BuildRoot = Join-Path $RootDir "build"
$I18nDir = Join-Path $RootDir "i18n"
$MetadataDir = Join-Path $RootDir "metadata"
$TsFile = Join-Path $I18nDir "${AppNameBase}_ru_RU.ts"
$QmFile = Join-Path $I18nDir "${AppNameBase}_ru_RU.qm"
$VersionHeader = Join-Path $RootDir "src\utils\VersionInfo.h"

if ($BuildQt) {
    $Static = $true
}

# Helper to find built executable in the centralized output directory
function Find-TargetExecutable([string]$ConfigName) {
    $Target = Join-Path $BuildRoot "bin\$ConfigName\$AppName"
    if (Test-Path $Target) {
        return $Target
    }
    return $null
}

# Inspects executable imports to distinguish between Shared (Debug/Release) and Static builds
function Get-BinaryQtLinkType([string]$FilePath) {
    if (-not (Test-Path $FilePath)) {
        return "NotFound"
    }

    # 1. Prefer dumpbin.exe if available in PATH
    $DumpBin = if (Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue) {
        "dumpbin.exe"
    } else {
        $null
    }
    if ($DumpBin) {
        $Deps = & $DumpBin /dependents $FilePath 2>$null
        if ($Deps -match "Qt6Cored\.dll") {
            return "SharedDebug"
        }
        if ($Deps -match "Qt6Core\.dll") {
            return "SharedRelease"
        }
        return "Static"
    }

    # 2. Fast chunked stream fallback to detect Qt DLL references in PE imports
    $Stream = [System.IO.File]::OpenRead($FilePath)
    try {
        $Buffer = New-Object byte[] 65536
        $Encoding = [System.Text.Encoding]::ASCII
        $Overlap = ""
        while (($BytesRead = $Stream.Read($Buffer, 0, $Buffer.Length)) -gt 0) {
            $Chunk = $Overlap + $Encoding.GetString($Buffer, 0, $BytesRead)
            if ($Chunk -match "Qt6Cored\.dll") {
                return "SharedDebug"
            }
            if ($Chunk -match "Qt6Core\.dll") {
                return "SharedRelease"
            }
            $Overlap = if ($Chunk.Length -gt 32) {
                $Chunk.Substring($Chunk.Length - 32)
            } else {
                $Chunk
            }
        }
        return "Static"
    } finally {
        $Stream.Close()
        $Stream.Dispose()
    }
}

# ─────────────────────────────────────────────────────────────
# 2. Kill Running Instance (if requested)
# ─────────────────────────────────────────────────────────────
if ($KillRunning) {
    Write-Step "Checking for running instances of $AppName..."
    Get-Process -Name $AppNameBase -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Write-Success "Terminated running instances."
}

# ─────────────────────────────────────────────────────────────
# 3. Translation Tools (lupdate / lrelease) & Qt Dir Detection
# ─────────────────────────────────────────────────────────────
# Auto-detect Qt Dir if needed
if (-not $QtDir) {
    if ($env:QT_DIR) {
        $QtDir = $env:QT_DIR
    } elseif ($env:Qt6_DIR) {
        $QtDir = $env:Qt6_DIR
    } else {
        $PresetsFile = Join-Path $RootDir "CMakePresets.json"
        if (Test-Path $PresetsFile) {
            try {
                $PresetsJson = Get-Content $PresetsFile -Raw | ConvertFrom-Json
                $SharedPreset = $PresetsJson.configurePresets | Where-Object { $_.name -eq "shared-base" }
                if ($SharedPreset -and $SharedPreset.cacheVariables.CMAKE_PREFIX_PATH) {
                    $PresetQt = $SharedPreset.cacheVariables.CMAKE_PREFIX_PATH
                    if (Test-Path $PresetQt) {
                        $QtDir = $PresetQt
                    }
                }
            } catch {
            }
        }
        if (-not $QtDir) {
            $QtCandidates = Get-ChildItem -Path "C:\Qt" -Filter "6.*" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
            foreach ($Candidate in $QtCandidates) {
                $MsvcPath = Join-Path $Candidate.FullName "msvc2022_64"
                if (Test-Path $MsvcPath) {
                    $QtDir = $MsvcPath
                    break
                }
            }
        }
    }
}

# Ensure Qt bin directory is in PATH for all tools (lupdate, lrelease, windeployqt)
if ($QtDir) {
    $QtBinDir = Join-Path $QtDir "bin"
    if (Test-Path $QtBinDir) {
        if ($env:PATH -notlike "*$QtBinDir*") {
            $env:PATH = "$QtBinDir;$env:PATH"
        }
    }
}

if ($Lupdate) {
    Write-Step "Running lupdate..."
    if (-not (Test-Path $I18nDir)) {
        New-Item -ItemType Directory -Path $I18nDir | Out-Null
    }
    $LupdateExe = Join-Path $QtDir "bin\lupdate.exe"
    $LupdateFlags = @("-locations", "none")
    if ($NoObsolete) {
        $LupdateFlags += "-noobsolete"
    }

    & $LupdateExe "$RootDir\src" @LupdateFlags -ts $TsFile
    Write-Success "lupdate completed."
}

# Compile translations if explicitly requested OR during a normal build if Qt is available
$ShouldCompileTranslations = $Lrelease -or (($Debug -or $Release -or $Package -or $Deploy) -and (Test-Path $TsFile))

if ($ShouldCompileTranslations -and $QtDir) {
    $LreleaseExe = Join-Path $QtDir "bin\lrelease.exe"
    if (Test-Path $LreleaseExe) {
        if (-not (Test-Path $TsFile)) {
            Write-Warn "TS file missing, running lupdate first..."
            & (Join-Path $QtDir "bin\lupdate.exe") "$RootDir\src" -locations none -ts $TsFile
        }
        & $LreleaseExe $TsFile
        Write-Success "lrelease completed (Binary translations updated)."
    }
}

# Exit early if ONLY translation tools were requested
if (($Lupdate -or $Lrelease) -and -not $Debug -and -not $Release -and -not $Package -and -not $Test -and -not $Clean -and -not $Format -and -not $Deploy) {
    $Stopwatch.Stop()
    $ElapsedSec = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 2)
    Write-Success "Translation step completed in $ElapsedSec seconds.`n"
    exit 0
}

# ─────────────────────────────────────────────────────────────
# 4. Code Formatting (clang-format)
# ─────────────────────────────────────────────────────────────
if ($Format) {
    Write-Step "Running clang-format on src/ and tests/..."
    $ClangFormat = "clang-format.exe"

    $TargetDirs = @("$RootDir\src", "$RootDir\tests") | Where-Object { Test-Path $_ }
    $Files = Get-ChildItem -Path $TargetDirs -Include *.cpp,*.h,*.hpp -Recurse
    $FormattedCount = 0

    foreach ($File in $Files) {
        $BeforeHash = (Get-FileHash -Path $File.FullName -Algorithm MD5).Hash
        & $ClangFormat -i $File.FullName
        $AfterHash = (Get-FileHash -Path $File.FullName -Algorithm MD5).Hash

        if ($BeforeHash -ne $AfterHash) {
            $RelPath = Resolve-Path -Path $File.FullName -Relative
            Write-Host "  [MODIFIED] $RelPath" -ForegroundColor Yellow
            $FormattedCount++
        }
    }

    if ($FormattedCount -gt 0) {
        Write-Success "clang-format reformatted $FormattedCount file(s)."
    } else {
        Write-Success "All source files in src/ and tests/ are already properly formatted."
    }
    exit 0
}

# ─────────────────────────────────────────────────────────────
# 5. vcpkg Static Qt Check & Build (--static / --build-qt)
# ─────────────────────────────────────────────────────────────
if ($Static) {
    Write-Step "Checking vcpkg static toolchain..."
    $VcpkgExe = if ($env:VCPKG_EXE) {
        $env:VCPKG_EXE
    } else {
        Join-Path $RootDir "vcpkg\vcpkg.exe"
    }

    if (-not (Test-Path $VcpkgExe)) {
        if (Get-Command "vcpkg.exe" -ErrorAction SilentlyContinue) {
            $VcpkgExe = (Get-Command "vcpkg.exe").Source
        }
    }

    if (-not (Test-Path $VcpkgExe)) {
        Write-Err "vcpkg.exe not found! Static build requires vcpkg. Please clone vcpkg or set VCPKG_EXE."
        exit 1
    }

    Write-Host "Using vcpkg: $VcpkgExe" -ForegroundColor Gray

    if ($BuildQt) {
        Write-Step "Installing/Building Qt static components via vcpkg (x64-windows-static)..."
        & $VcpkgExe install "qtbase[openssl]" qttools qtsvg qttranslations --triplet=x64-windows-static
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Failed to build Qt static libraries via vcpkg!"
            exit $LASTEXITCODE
        }
        Write-Success "Qt static libraries installed successfully."
    }
}

# ─────────────────────────────────────────────────────────────
# 6. MSVC Toolchain & Preset Setup
# ─────────────────────────────────────────────────────────────
if (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
    Write-Step "Locating Visual Studio MSVC environment via vswhere..."
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWhere) {
        $InstallPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($InstallPath) {
            $VcVars = Join-Path $InstallPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $VcVars) {
                Write-Host "Initializing MSVC x64 environment from $VcVars" -ForegroundColor Gray
                $EnvBlock = cmd /c "`"$VcVars`" > NUL && set"
                foreach ($Line in $EnvBlock) {
                    if ($Line -match "^(?<Key>[^=]+)=(?<Val>.*)$") {
                        [System.Environment]::SetEnvironmentVariable($Matches['Key'], $Matches['Val'], [System.EnvironmentVariableTarget]::Process)
                    }
                }
            }
        }
    }
}

# Preset variables
$Gen = "ninja"
$Type = if ($Static) {
    "static"
} else {
    "shared"
}
$PresetName = "$Gen-$Type"
$PresetBuildDir = Join-Path $BuildRoot $PresetName

# Determine build / deploy configurations
$ConfigsToBuild = @()
if ($Debug) {
    $ConfigsToBuild += "Debug"
}
if ($Release) {
    $ConfigsToBuild += "Release"
}

# If user specified --deploy without explicit -d or -r:
# Smart auto-detection between Shared vs Static builds
if ($Deploy -and ($ConfigsToBuild.Count -eq 0)) {
    $ExistingDebug = Find-TargetExecutable "Debug"
    $ExistingRelease = Find-TargetExecutable "Release"

    $DebugType = if ($ExistingDebug) {
        Get-BinaryQtLinkType $ExistingDebug
    } else {
        "NotFound"
    }
    $ReleaseType = if ($ExistingRelease) {
        Get-BinaryQtLinkType $ExistingRelease
    } else {
        "NotFound"
    }

    $DebugIsShared = ($DebugType -like "Shared*")
    $ReleaseIsShared = ($ReleaseType -like "Shared*")

    if ($DebugIsShared -and -not $ReleaseIsShared) {
        $ConfigsToBuild = @("Debug")
        Write-Host "[DEPLOY] Detected shared Qt build in Debug (Release is static or not built). Targeting Debug." -ForegroundColor Gray
    } elseif ($ReleaseIsShared -and -not $DebugIsShared) {
        $ConfigsToBuild = @("Release")
        Write-Host "[DEPLOY] Detected shared Qt build in Release (Debug is static or not built). Targeting Release." -ForegroundColor Gray
    } elseif ($DebugIsShared -and $ReleaseIsShared) {
        # Both are shared: pick the newer one
        $DebugTime = (Get-Item $ExistingDebug).LastWriteTime
        $ReleaseTime = (Get-Item $ExistingRelease).LastWriteTime
        if ($DebugTime -ge $ReleaseTime) {
            $ConfigsToBuild = @("Debug")
            Write-Host "[DEPLOY] Both builds are shared. Selected newest: Debug ($DebugTime)." -ForegroundColor Gray
        } else {
            $ConfigsToBuild = @("Release")
            Write-Host "[DEPLOY] Both builds are shared. Selected newest: Release ($ReleaseTime)." -ForegroundColor Gray
        }
    } elseif ($DebugType -eq "Static" -or $ReleaseType -eq "Static") {
        Write-Warn "Found binary is a static build (no Qt DLL dependencies). windeployqt is not required."
        exit 0
    } else {
        # Neither exists: default to Debug for local shared development
        $ConfigsToBuild = @("Debug")
        Write-Host "[DEPLOY] No existing shared build found. Defaulting to Debug." -ForegroundColor Gray
    }
} elseif ($ConfigsToBuild.Count -eq 0) {
    # Default for full standard build without flags is both Debug and Release
    $ConfigsToBuild = @("Debug", "Release")
}

# ─────────────────────────────────────────────────────────────
# 6.1 Standalone Deploy Shortcut (Fast-path if already compiled)
# ─────────────────────────────────────────────────────────────
$IsStandaloneDeploy = $Deploy -and -not $Clean -and -not $Test -and -not $Package -and -not $BuildQt

if ($IsStandaloneDeploy) {
    if ($Static) {
        Write-Warn "Deploy (windeployqt) is only applicable to shared Qt builds (-Static was specified)."
        exit 0
    }

    $AllExist = $true
    $TargetsToDeploy = @()

    foreach ($Cfg in $ConfigsToBuild) {
        $FoundExe = Find-TargetExecutable $Cfg
        if ($FoundExe) {
            $TargetsToDeploy += @{ Config = $Cfg; Exe = $FoundExe }
        } else {
            $AllExist = $false
        }
    }

    # If all requested targets exist, deploy immediately without rebuilding!
    if ($AllExist -and $TargetsToDeploy.Count -gt 0) {
        Write-Step "Running standalone Qt deployment (windeployqt)..."

        $WinDeployQt = if ($QtDir) {
            Join-Path $QtDir "bin\windeployqt.exe"
        } else {
            $null
        }
        if (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
            if (Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue) {
                $WinDeployQt = (Get-Command "windeployqt.exe").Source
            }
        }

        if (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
            Write-Err "windeployqt.exe not found! Please specify -QtDir or set QT_DIR."
            exit 1
        }

        foreach ($Target in $TargetsToDeploy) {
            $CfgName = $Target.Config
            $TargetExe = $Target.Exe
            $LinkType = Get-BinaryQtLinkType $TargetExe

            if ($LinkType -eq "Static") {
                Write-Warn "'$TargetExe' is a static build (no Qt DLL dependencies). Skipping windeployqt."
                continue
            }

            $CfgFlag = "--$($CfgName.ToLower())"
            Write-Host "Deploying Qt dependencies for $CfgName to $(Split-Path $TargetExe -Parent)..." -ForegroundColor Cyan
            & $WinDeployQt $CfgFlag --compiler-runtime $TargetExe
            if ($LASTEXITCODE -ne 0) {
                Write-Err "windeployqt failed for $CfgName with exit code $LASTEXITCODE!"
                exit $LASTEXITCODE
            }
            Write-Success "windeployqt completed successfully for $CfgName."
        }

        $Stopwatch.Stop()
        $ElapsedSec = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 2)
        Write-Success "Deployment step completed in $ElapsedSec seconds.`n"
        exit 0
    } else {
        Write-Host "[DEPLOY] Target executable not found. Triggering build before deployment..." -ForegroundColor Gray
    }
}

if ($Clean -and (Test-Path $PresetBuildDir)) {
    Write-Step "Cleaning preset build directory: $PresetBuildDir..."
    Remove-Item -Path $PresetBuildDir -Recurse -Force
    Write-Success "Clean completed."
}

# ─────────────────────────────────────────────────────────────
# 7. Configure, Build, and Deploy Loop
# ─────────────────────────────────────────────────────────────
foreach ($Config in $ConfigsToBuild) {
    Write-Step "Configuring and Building Preset: $PresetName ($Config)..."

    $CmakeArgs = @(
        "--preset", $PresetName,
        "-DGITHUB_URL=$($env:GITHUB_URL)",
        "-DUPDATE_URL=$($env:UPDATE_URL)",
        "-DLICENSE_URL=$($env:LICENSE_URL)",
        "-DBUILD_TESTING=$(if ($Test) { 'ON' } else { 'OFF' })"
    )

    & cmake @CmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Err "CMake configuration failed!"
        exit $LASTEXITCODE
    }

    & cmake --build --preset $PresetName --config $Config --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Build failed!"
        exit $LASTEXITCODE
    }

    # Run CTest
    if ($Test) {
        Write-Step "Running CTest suite for $Config..."
        Set-Location $PresetBuildDir
        & ctest --output-on-failure -C $Config
        $TestResult = $LASTEXITCODE
        Set-Location $RootDir
        if ($TestResult -ne 0) {
            Write-Err "Tests failed!"
            exit $TestResult
        }
        Write-Success "All unit tests passed."
    }

    # Run windeployqt for shared builds
    if ($Deploy -and -not $Static) {
        Write-Step "Deploying Qt libraries with windeployqt ($Config)..."

        $TargetExe = Find-TargetExecutable $Config
        $LinkType = if ($TargetExe) {
            Get-BinaryQtLinkType $TargetExe
        } else {
            "NotFound"
        }

        if ($LinkType -eq "Static") {
            Write-Warn "'$TargetExe' is a static build. Skipping windeployqt."
            continue
        }

        $WinDeployQt = if ($QtDir) {
            Join-Path $QtDir "bin\windeployqt.exe"
        } else {
            $null
        }
        if (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
            if (Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue) {
                $WinDeployQt = (Get-Command "windeployqt.exe").Source
            }
        }

        if ($TargetExe -and $WinDeployQt -and (Test-Path $WinDeployQt)) {
            $CfgFlag = "--$($Config.ToLower())"
            & $WinDeployQt $CfgFlag --compiler-runtime $TargetExe
            if ($LASTEXITCODE -ne 0) {
                Write-Err "windeployqt failed for $Config with exit code $LASTEXITCODE!"
                exit $LASTEXITCODE
            }
            Write-Success "windeployqt completed successfully for $Config."
        } elseif (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
            Write-Err "windeployqt.exe not found! Please check QT_DIR."
        } else {
            Write-Warn "Executable $AppName not found for windeployqt execution ($Config)."
        }
    }

    # ─────────────────────────────────────────────────────────
    # 8. Selective Release Packaging & Manifest Generation
    # ─────────────────────────────────────────────────────────
    if ($Package) {
        Write-Step "Packaging Release artifacts for $Config..."

        $AppExe = Find-TargetExecutable $Config

        if (-not $AppExe) {
            Write-Err "Executable '$AppName' not found for configuration '$Config'!"
            exit 1
        }

        $BinDir = Split-Path $AppExe -Parent
        Write-Host "Found target executable ($Config) at: $AppExe" -ForegroundColor Gray

        $ArtifactsDir = Join-Path $BuildRoot "artifacts"
        if (-not (Test-Path $ArtifactsDir)) {
            New-Item -ItemType Directory -Path $ArtifactsDir -Force | Out-Null
        }

        $AppVersion = "0.0.1"
        if (Test-Path $VersionHeader) {
            $HeaderContent = Get-Content $VersionHeader -Raw
            $maj = if ($HeaderContent -match '#define\s+APP_VERSION_MAJOR\s+(\d+)') {
                $Matches[1]
            } else {
                "0"
            }
            $min = if ($HeaderContent -match '#define\s+APP_VERSION_MINOR\s+(\d+)') {
                $Matches[1]
            } else {
                "0"
            }
            $pat = if ($HeaderContent -match '#define\s+APP_VERSION_PATCH\s+(\d+)') {
                $Matches[1]
            } else {
                "1"
            }
            $AppVersion = "$maj.$min.$pat"
        }

        $ArchiveBaseName = "$AppNameBase-v$AppVersion-win-x64"
        if ($Config -ne "Release") {
            $ArchiveBaseName += "-$Config"
        }
        $ZipPath = Join-Path $ArtifactsDir "$ArchiveBaseName.zip"

        $StagingDir = Join-Path $ArtifactsDir "staging_$ArchiveBaseName"
        if (Test-Path $StagingDir) {
            Remove-Item -Path $StagingDir -Recurse -Force
        }
        New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

        Copy-Item -Path $AppExe -Destination $StagingDir -Force

        if (-not $Static) {
            Get-ChildItem -Path $BinDir -Filter "*.dll" | Copy-Item -Destination $StagingDir -Force

            $WinDeployQt = if ($QtDir) {
                Join-Path $QtDir "bin\windeployqt.exe"
            } else {
                $null
            }
            if (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
                if (Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue) {
                    $WinDeployQt = (Get-Command "windeployqt.exe").Source
                }
            }

            if ($WinDeployQt -and (Test-Path $WinDeployQt)) {
                Write-Host "Running windeployqt on staging folder for portable shared package..." -ForegroundColor Gray
                $StagingExe = Join-Path $StagingDir $AppName
                $CfgFlag = "--$($Config.ToLower())"
                & $WinDeployQt $CfgFlag --compiler-runtime $StagingExe
                Write-Success "Qt dependencies deployed to staging directory."
            } else {
                Write-Warn "windeployqt.exe not found, skipping Qt DLL deployment."
            }
        }

        $LicenseSrc = Get-ChildItem -Path $RootDir -Filter "LICENSE*" | Select-Object -First 1
        if ($LicenseSrc) {
            Copy-Item -Path $LicenseSrc.FullName -Destination (Join-Path $StagingDir "LICENSE.txt") -Force
        }

        $ReadmeSrc = Join-Path $RootDir "README.md"
        if (Test-Path $ReadmeSrc) {
            Copy-Item -Path $ReadmeSrc -Destination $StagingDir -Force
        }

        $NoticesSrc = Join-Path $RootDir "THIRD_PARTY_NOTICES.md"
        if (Test-Path $NoticesSrc) {
            Copy-Item -Path $NoticesSrc -Destination (Join-Path $StagingDir "THIRD_PARTY_NOTICES.txt") -Force
        }

        if (Test-Path $ZipPath) {
            Remove-Item -Path $ZipPath -Force
        }
        Write-Host "Compressing distribution archive: $ZipPath" -ForegroundColor Gray
        Compress-Archive -Path "$StagingDir\*" -DestinationPath $ZipPath -Force

        Remove-Item -Path $StagingDir -Recurse -Force

        $Hash = Get-Sha256Checksum $ZipPath
        $ShaFile = "$ZipPath.sha256"
        Set-Content -Path $ShaFile -Value "$Hash *$ArchiveBaseName.zip" -NoNewline

        if (-not (Test-Path $MetadataDir)) {
            New-Item -ItemType Directory -Path $MetadataDir -Force | Out-Null
        }

        # Generate pre-rendered release notes for GitHub Publication from changelog.md
        $ChangelogFile = Join-Path $MetadataDir "changelog.md"
        if (Test-Path $ChangelogFile) {
            $RawChangelog = Get-Content $ChangelogFile -Raw
            $RenderedChangelog = $RawChangelog.Replace('{VERSION}', $AppVersion)
            $ReleaseNotesDst = Join-Path $ArtifactsDir "release_notes.md"
            $Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
            [System.IO.File]::WriteAllText($ReleaseNotesDst, $RenderedChangelog, $Utf8NoBom)
            Write-Success "Release Notes: $ReleaseNotesDst"
        }

        Write-Success "ZIP Package: $ZipPath"
        Write-Success "SHA-256 Checksum: $ShaFile"
    }
}

# ─────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────
$Stopwatch.Stop()
$ElapsedSec = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 2)
Write-Success "Master Build Pipeline finished in $ElapsedSec seconds.`n"
