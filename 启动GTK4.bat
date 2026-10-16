@echo off
setlocal

cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

if not exist "build\charging_system_gtk4.exe" (
    echo GTK 4 executable was not found:
    echo %~dp0build\charging_system_gtk4.exe
    pause
    exit /b 1
)

start "" "build\charging_system_gtk4.exe"

