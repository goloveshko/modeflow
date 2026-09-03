@echo off
rem Convenience wrapper: build.bat --release --static --ninja [...]
call "%~dp0build.bat" --release --static --ninja %*