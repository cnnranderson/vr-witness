@echo off
setlocal
title Witness VR
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-vr.ps1" %*
set "launcherResult=%ERRORLEVEL%"
echo.
if not "%launcherResult%"=="0" echo Startup did not finish. See the message above.
pause
exit /b %launcherResult%
