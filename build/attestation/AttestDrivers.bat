@echo off
echo Running attestation signing

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "HEADER_FILE=%SCRIPT_DIR%..\..\src\windows\qcversion.h"
set "MACRO_NAME=QCOM_USB_DRIVERS_PRODUCT_VERSION"
set "ATTEST_SCRIPT=%SCRIPT_DIR%Attestation.ps1"
set "CAB_PATH=%SCRIPT_DIR%..\target\drivers.cab"
set "VERSION="

for /f "tokens=3" %%V in ('findstr /r /c:"#define %MACRO_NAME% " "%HEADER_FILE%"') do (
    set "VERSION=%%V"
)

if "!VERSION!"=="" (
    echo [ERROR] Could not read version from %HEADER_FILE%
    exit /b 1
)
echo [INFO] QUD Version is: !VERSION!

if not exist "!ATTEST_SCRIPT!" (
    echo [ERROR] Attestation script not found: !ATTEST_SCRIPT!
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!ATTEST_SCRIPT!" -ProductName "QCOM_USB_KERNEL_DRIVERS_!VERSION!" -Signatures "WINDOWS_v100_X64_RS4_FULL" -InputPath "!CAB_PATH!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Attestation signing failed.
    exit /b !ERRORLEVEL!
)

echo [OK] Attestation process complete.

endlocal
exit /b 0
