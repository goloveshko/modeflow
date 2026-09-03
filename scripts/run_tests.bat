@echo off
rem Convenience wrapper: build.bat --debug --ninja --tests
call "%~dp0build.bat" --debug --ninja --tests %*