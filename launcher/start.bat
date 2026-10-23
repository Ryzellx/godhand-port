@echo off
rem God Hand Launcher - Windows
cd /d "%~dp0"
where py >nul 2>nul && (py -3 launcher.py %*) || (python launcher.py %*)
if errorlevel 1 pause
