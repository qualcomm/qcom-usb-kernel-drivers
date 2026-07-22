:: Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
:: SPDX-License-Identifier: BSD-3-Clause

@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "BUILD_ROOT=%SCRIPT_DIR%target"
set "NO_SIGN=0"

for %%A in (%*) do (
    if /i "%%A"=="--no_sign_required" set "NO_SIGN=1"
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\build_drivers.ps1" -OutputTo "%BUILD_ROOT%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\sign.ps1" -InputFrom "%BUILD_ROOT%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

if "%NO_SIGN%"=="1" (
    echo [INFO] --no_sign_required set: skipping EV signing and attestation.
    call "%SCRIPT_DIR%build_installer.bat" --no_sign_required
) else (
    call "%SCRIPT_DIR%attestation\AttestDrivers.bat"
    if !ERRORLEVEL! neq 0 exit /b !ERRORLEVEL!
    call "%SCRIPT_DIR%build_installer.bat"
)
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

:done
echo [DONE] Output: %BUILD_ROOT%

endlocal
exit /b 0
