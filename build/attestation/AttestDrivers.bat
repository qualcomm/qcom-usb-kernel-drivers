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

set "SIG_X64_RS4=WINDOWS_v100_X64_RS4_FULL"
set "SIG_X64_CO=WINDOWS_v100_X64_CO_FULL"
set "SIG_X64_NI=WINDOWS_v100_X64_NI_FULL"
set "SIG_X64_GE=WINDOWS_v100_X64_GE_FULL"
set "SIG_X64_SV3=WINDOWS_v100_X64_SV3_FULL"
set "SIG_ARM64_RS4=WINDOWS_v100_ARM64_RS4_FULL"
set "SIG_ARM64_CO=WINDOWS_v100_ARM64_CO_FULL"
set "SIG_ARM64_NI=WINDOWS_v100_ARM64_NI_FULL"
set "SIG_ARM64_GE=WINDOWS_v100_ARM64_GE_FULL"
set "SIG_ARM64_SV3=WINDOWS_v100_ARM64_SV3_FULL"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!ATTEST_SCRIPT!" -ProductName "QUD_!VERSION!_EXT" -Signatures "!SIG_X64_RS4!","!SIG_X64_CO!","!SIG_X64_NI!","!SIG_X64_GE!","!SIG_X64_SV3!","!SIG_ARM64_RS4!","!SIG_ARM64_CO!","!SIG_ARM64_NI!","!SIG_ARM64_GE!","!SIG_ARM64_SV3!" -InputPath "!CAB_PATH!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Attestation signing failed.
    exit /b !ERRORLEVEL!
)

echo [OK] Attestation process complete.

endlocal
exit /b 0
