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

set "SIGNATURES=WINDOWS_v100_X64_RS4_FULL,WINDOWS_v100_X64_CO_FULL,WINDOWS_v100_X64_NI_FULL,WINDOWS_v100_X64_GE_FULL,WINDOWS_v100_X64_SV3_FULL,WINDOWS_v100_ARM64_RS4_FULL,WINDOWS_v100_ARM64_CO_FULL,WINDOWS_v100_ARM64_NI_FULL,WINDOWS_v100_ARM64_GE_FULL,WINDOWS_v100_ARM64_SV3_FULL"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!ATTEST_SCRIPT!" -ProductName "QUD_!VERSION!_EXT" -Signatures !SIGNATURES! -InputPath "!CAB_PATH!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Attestation signing failed.
    exit /b !ERRORLEVEL!
)

echo [OK] Attestation process complete.

endlocal
exit /b 0
