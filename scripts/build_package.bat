@echo off
rem Convenience wrapper: build.bat --release --static --ninja --package [...]
call "%~dp0build.bat" --release --static --ninja --package %*