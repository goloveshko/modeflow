@echo off
rem Convenience wrapper: build.cmd --release --static --ninja [...]
call "%~dp0build.cmd" --release --static --ninja %*