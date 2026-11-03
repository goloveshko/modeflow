@echo off
rem Convenience wrapper: build.cmd --debug --ninja  [...]
call "%~dp0build.cmd" --debug --ninja %*