@echo off
REM AAAos QEMU Launch Script for Windows
REM Runs the OS in QEMU emulator

setlocal

set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..
set IMAGE=%PROJECT_DIR%\build\aaaos.img

REM Check if image exists
if not exist "%IMAGE%" (
    echo Error: Disk image not found at %IMAGE%
    echo Please build the project first
    exit /b 1
)

echo Starting AAAos in QEMU...
echo Press Ctrl+A then X to exit QEMU
echo.

REM Try to find QEMU in common locations
set QEMU=qemu-system-x86_64

where %QEMU% >nul 2>&1
if %errorlevel% neq 0 (
    if exist "C:\Program Files\qemu\qemu-system-x86_64.exe" (
        set QEMU="C:\Program Files\qemu\qemu-system-x86_64.exe"
    ) else if exist "C:\qemu\qemu-system-x86_64.exe" (
        set QEMU="C:\qemu\qemu-system-x86_64.exe"
    ) else (
        echo Error: QEMU not found. Please install QEMU or add it to PATH.
        exit /b 1
    )
)

%QEMU% -drive format=raw,file="%IMAGE%" -serial stdio -m 256M -no-reboot -no-shutdown %*
