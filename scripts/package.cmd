@echo off
rem Convenience wrapper: build.cmd --release --static --ninja --package [...]
call "%~dp0build.cmd" --release --static --ninja --package %*