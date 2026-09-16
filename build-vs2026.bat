@echo off
setlocal

:: Build pc_virtual_keyboard with Visual Studio 2026 (x64 Release)
:: Usage: build-vs2026.bat [debug]

set PRESET=vs2026-x64
set BUILD_PRESET=vs2026-x64-release
set CONFIG=Release

if /i "%~1"=="debug" (
    set BUILD_PRESET=vs2026-x64-debug
    set CONFIG=Debug
)

echo === Configuring (%PRESET%) ===
cmake --preset %PRESET%
if errorlevel 1 (
    echo Configure failed.
    exit /b 1
)

echo.
echo === Building (%BUILD_PRESET%) ===
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo.
echo === Done ===
echo Output: build\%PRESET%\%CONFIG%\pc_virtual_keyboard.exe
endlocal
