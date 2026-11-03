@echo off
rem Harmony Recomp - one-click Windows build. Double-click, or run from a
rem command prompt with options, for example:
rem   build-windows.bat -InstallPrerequisites -Dumps D:\HoD
rem Options: see scripts\build_windows.ps1 or docs\WINDOWS.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_windows.ps1" %*
set RESULT=%ERRORLEVEL%
echo.
pause
exit /b %RESULT%
