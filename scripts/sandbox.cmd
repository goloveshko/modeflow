@echo off
setlocal
rem Convenience wrapper: sandbox.ps1 -SkipBuild [...]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sandbox.ps1" -SkipBuild %*
exit /b %ERRORLEVEL%
