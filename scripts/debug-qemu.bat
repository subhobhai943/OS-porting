@echo off
REM AAAos QEMU Debug Script for Windows
REM Runs the OS in QEMU with GDB server enabled

setlocal

set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..
set IMAGE=%PROJECT_DIR%\build\aaaos.img
set KERNEL=%PROJECT_DIR%\build\kernel.bin

REM Check if image exists
if not exist "%IMAGE%" (
    echo Error: Disk image not found at %IMAGE%
    echo Please build the project first
    exit /b 1
)

echo Starting AAAos in QEMU debug mode...
echo.
echo QEMU is waiting for GDB connection on localhost:1234
echo In another terminal, run:
echo.
echo   gdb -ex "target remote localhost:1234" %KERNEL%
echo.
echo Useful GDB commands:
echo   c          - Continue execution
echo   b *0x100000 - Set breakpoint at kernel entry
echo   si         - Single step instruction
echo   info reg   - Show registers
echo.
echo Press Ctrl+C to stop QEMU
echo.

REM Try to find QEMU
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

%QEMU% -drive format=raw,file="%IMAGE%" -serial stdio -m 256M -no-reboot -no-shutdown -s -S %*
