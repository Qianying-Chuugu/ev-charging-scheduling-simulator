@echo off
setlocal

cd /d "%~dp0"
set "TOOL_BIN=C:\msys64\ucrt64\bin"
set "PATH=%TOOL_BIN%;%PATH%"

if not exist "%TOOL_BIN%\gcc.exe" (
    echo Build failed: %TOOL_BIN%\gcc.exe was not found.
    goto :failed
)

if not exist "%TOOL_BIN%\pkg-config.exe" (
    echo Build failed: %TOOL_BIN%\pkg-config.exe was not found.
    goto :failed
)

pkg-config --exists gtk4
if errorlevel 1 (
    echo Build failed: pkg-config could not find GTK 4.
    goto :failed
)

for /f "delims=" %%I in ('pkg-config --cflags gtk4') do set "GTK_CFLAGS=%%I"
for /f "delims=" %%I in ('pkg-config --libs gtk4') do set "GTK_LIBS=%%I"

if not exist "build" mkdir "build"

echo Building the GTK 4 application...
gcc -std=c11 -Wall -Wextra -pedantic -Isrc %GTK_CFLAGS% ^
    src\module8_ui\gtk4_ui.c ^
    src\module9_timer\timer.c ^
    src\module1_graph\graph.c ^
    src\module2_scheduler\scheduler.c ^
    src\module3_entity\entity.c ^
    src\module4_charger\charger.c ^
    src\module5_recommend\recommend.c ^
    src\module6_simulation\simulation.c ^
    src\module7_fileio\fileio.c ^
    %GTK_LIBS% -lm -o build\charging_system_gtk4.exe

if errorlevel 1 goto :failed

echo Build succeeded: build\charging_system_gtk4.exe
if /i not "%~1"=="--no-pause" pause
exit /b 0

:failed
if /i not "%~1"=="--no-pause" pause
exit /b 1
