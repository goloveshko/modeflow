# ============================================================================
# ModeFlow – Local Environment Configuration Template
# ============================================================================
# Copy this file to "environment.ps1" in the scripts/ directory to override
# local toolchain paths and test server endpoints without modifying build.ps1.
#
# Do NOT commit "environment.ps1" to version control (it is ignored by git).
# ============================================================================

# --- 1. Local Toolchain Paths (Always Applied) ------------------------------
# Override toolchain directories if they differ from system defaults.
#
# $env:QT_DIR     = "C:\Qt\6.11.2\msvc2022_64"
# $env:VCPKG_ROOT = "C:\dev\vcpkg"
# $env:VCPKG_EXE  = "C:\dev\vcpkg\vcpkg.exe"


# --- 2. Local Test Server Endpoints (Dev/Debug Builds Only) -----------------
# Override repository and API endpoints when testing against local Forgejo/Gitea.
# Release packaging (--package / build_package.bat) replaces all three with the
# official production URLs, so these only affect dev and debug builds.
#
# $env:GITHUB_URL  = "http://localhost:3000/{owner}/{repo}"
# $env:UPDATE_URL  = "http://localhost:3000/api/v1/repos/{owner}/{repo}/releases/latest"
# $env:LICENSE_URL = "$($env:GITHUB_URL)/src/branch/main/LICENSE"
