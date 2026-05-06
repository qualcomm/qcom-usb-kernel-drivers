@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

for %%A in (x86 x64 arm64) do (
    echo ========================================
    echo  Building for %%A
    echo ========================================

    REM Build tools for this architecture
    powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build_tools.ps1" -Platform %%A
    if !ERRORLEVEL! neq 0 exit /b !ERRORLEVEL!

    REM Build installer with arch-specific output name
    powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%Build-Installer.ps1" -OutputPath "%SCRIPT_DIR%target\qcom_usb_kernel_drivers_%%A.exe"
    if !ERRORLEVEL! neq 0 exit /b !ERRORLEVEL!

    REM Sign the installer
    powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sign.ps1" -InputFrom "%SCRIPT_DIR%target\qcom_usb_kernel_drivers_%%A.exe"
    if !ERRORLEVEL! neq 0 (
        echo [WARN] Signing failed for %%A. The unsigned exe is still available.
    )

    echo [DONE] %SCRIPT_DIR%target\QUD_Installer_%%A.exe
    echo.
)

echo ========================================
echo  All installers built.
echo ========================================

endlocal
exit /b 0