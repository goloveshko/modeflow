@echo off
rem Convenience wrapper: build.cmd --debug --ninja --tests
call "%~dp0build.cmd" --debug --ninja --tests %*