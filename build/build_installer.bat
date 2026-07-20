:: Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
:: SPDX-License-Identifier: BSD-3-Clause

@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "NO_SIGN=0"

for %%A in (%*) do (
    if /i "%%A"=="--no_sign_required" set "NO_SIGN=1"
)

for %%A in (x86 x64 arm64) do (
    REM Build tools for this architecture
    powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build_tools.ps1" -Platform %%A
    if !ERRORLEVEL! neq 0 exit /b !ERRORLEVEL!

    REM Build installer with arch-specific output name and architecture
    if "!NO_SIGN!"=="1" (
        powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-installer.ps1" -OutputName "qcom_usb_kernel_drivers_%%A.exe" -Arch %%A -SkipSignCheck
    ) else (
        powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-installer.ps1" -OutputName "qcom_usb_kernel_drivers_%%A.exe" -Arch %%A
    )
    if !ERRORLEVEL! neq 0 exit /b !ERRORLEVEL!

    REM Sign the installer
    powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sign.ps1" -InputFrom "%SCRIPT_DIR%target\qcom_usb_kernel_drivers_%%A.exe"
    if !ERRORLEVEL! neq 0 (
        echo [WARN] Signing failed for %%A. The unsigned exe is still available.
    )

    echo [DONE] %SCRIPT_DIR%target\qcom_usb_kernel_drivers_%%A.exe
)

endlocal
exit /b 0