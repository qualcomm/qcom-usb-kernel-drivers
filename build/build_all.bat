@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "BUILD_ROOT=%SCRIPT_DIR%\target"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\build.ps1" -OutputTo "%BUILD_ROOT%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\sign.ps1" -InputFrom "%BUILD_ROOT%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo [DONE] Output: %BUILD_ROOT%

endlocal
exit /b 0
