:: Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
:: SPDX-License-Identifier: BSD-3-Clause

@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "BUILD_ROOT=%SCRIPT_DIR%target"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build_drivers.ps1" -OutputTo "%BUILD_ROOT%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sign.ps1"
if %ERRORLEVEL% neq 0 echo [WARNING] EV-sign failed, unsigned drivers are built.

echo [DONE] Output: %BUILD_ROOT%

endlocal
exit /b 0
