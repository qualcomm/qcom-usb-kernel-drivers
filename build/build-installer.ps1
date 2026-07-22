# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

param(
    [string]$OutputName = "installer.exe",
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Arch = "x64",   # default value (adjust if needed)
    [switch]$SkipSignCheck
)

# ==============================================================================
# Configuration
# ==============================================================================

$Script:OutputRoot   = Join-Path $PSScriptRoot "target"
$Script:PayloadName  = "payload_$Arch.zip"
$Script:VersionFile  = Join-Path $PSScriptRoot "..\src\windows\qcversion.h"

# Items to include in the payload zip (files or directories under target/).
# Promote: optional list of file names to move to the payload root.
$Script:PayloadItems = @(
    @{ Path = "drivers"; Arch = $null; Promote = $null }
    @{ Path = "tools";   Arch = $null; Promote = @("qdclr.exe", "qdinstall.exe") }
)

# Only .cat files must be Microsoft-attested signed before the installer can be built.
# .sys files and .cab are signed by QCOM EV signing and are not checked here.
# Paths are relative to $Script:OutputRoot.
$Script:RequiredSignedFiles = @(
    "drivers\qcadb.cat"
    "drivers\qcfilter.cat"
    "drivers\qcwwan.cat"
    "drivers\qcwdfserial.cat"
    "drivers\qdbusb.cat"
)

# ==============================================================================
# Functions
# ==============================================================================

# Locates signtool.exe from WDK or PATH.
function Find-SignTool {
    $cmd = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $regPath = "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"
    $kitRoot = (Get-ItemProperty -Path $regPath -Name "KitsRoot10" -ErrorAction SilentlyContinue).KitsRoot10
    if ($kitRoot) {
        $versions = Get-ChildItem -Path (Join-Path $kitRoot "bin") -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [Version]$_.Name } -Descending
        foreach ($ver in $versions) {
            $candidate = Join-Path $ver.FullName "x64\signtool.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

# Verifies all required files are signed. Errors out listing every unsigned file.
function Assert-DriversSigned {
    Write-Host "========================================"
    Write-Host " Verifying Driver Signatures"
    Write-Host "========================================`n"

    $signtool = Find-SignTool
    if (-not $signtool) {
        Write-Host "[ERROR] signtool.exe not found. Please install the Windows Driver Kit (WDK)." -ForegroundColor Red
        exit 1
    }
    Write-Host "[INFO] Using signtool: $signtool`n"

    $unsigned = @()
    $missing  = @()

    foreach ($rel in $Script:RequiredSignedFiles) {
        $fullPath = Join-Path $Script:OutputRoot $rel

        if (-not (Test-Path $fullPath)) {
            $missing += $rel
            continue
        }

        # signtool verify /pa = verify against default auth policy (works for both EV and attested)
        $result = & $signtool verify /pa /q $fullPath 2>&1
        if ($LASTEXITCODE -ne 0) {
            $unsigned += $rel
            Write-Host "[UNSIGNED] $rel" -ForegroundColor Red
        } else {
            Write-Host "[SIGNED]   $rel" -ForegroundColor Green
        }
    }

    Write-Host ""

    if ($missing.Count -gt 0) {
        Write-Host "[ERROR] The following required files are missing:" -ForegroundColor Red
        $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        Write-Host ""
    }

    if ($unsigned.Count -gt 0) {
        Write-Host "[ERROR] The following files are not signed:" -ForegroundColor Red
        $unsigned | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        Write-Host ""
    }

        if ($missing.Count -gt 0 -or $unsigned.Count -gt 0) {
        Write-Host "[ERROR] Catalog signature verification failed. Run AttestDrivers.bat to get Microsoft-attested .cat files before building the installer." -ForegroundColor Red
        exit 1
    }

    Write-Host "[OK] All required files are signed.`n" -ForegroundColor Green
}

# Assembles a payload zip from target/drivers and target/tools.
function New-Payload {
    Write-Host "========================================"
    Write-Host " Packaging Payload"
    Write-Host "========================================`n"

    # Create a temp staging directory
    $stagingDir = Join-Path ([System.IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

    try {
        # Copy all payload items into the staging root.
        foreach ($item in $Script:PayloadItems) {
            if ($item.Arch) {
                $archSource = Join-Path $PSScriptRoot "$($item.Path)\$($item.Arch)"
                $destDir    = Join-Path $stagingDir $item.Path
                if (-not (Test-Path $archSource)) {
                    Write-Warning "[WARNING] $($item.Path) arch folder not found: $archSource"
                    continue
                }
                New-Item -ItemType Directory -Path $destDir -Force | Out-Null
                Copy-Item -Path "$archSource\*" -Destination $destDir -Recurse -Force
                Write-Host "[COPY] $($item.Path)/$($item.Arch)/* -> $($item.Path)/"
                continue
            }

            $src = Join-Path $Script:OutputRoot $item.Path
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination $stagingDir -Recurse -Force
                Write-Host "[COPY] $($item.Path) -> staging"
            } else {
                Write-Warning "[WARNING] Payload item not found: $src"
                continue
            }

            if ($item.Promote) {
                foreach ($fileName in $item.Promote) {
                    $srcFile = Join-Path (Join-Path $stagingDir $item.Path) $fileName
                    if (Test-Path $srcFile) {
                        Copy-Item -Path $srcFile -Destination $stagingDir -Force
                        Remove-Item -Path $srcFile -Force
                        Write-Host "[PROMOTE] $($item.Path)/$fileName -> payload root"
                    } else {
                        Write-Warning "[WARNING] Promoted file not found: $($item.Path)/$fileName"
                    }
                }
            }
        }

        # Create the zip
        $payloadZip = Join-Path $Script:OutputRoot $Script:PayloadName
        if (Test-Path $payloadZip) {
            Remove-Item $payloadZip -Force
        }

        Write-Host "[ZIP] Creating: $payloadZip"
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::CreateFromDirectory($stagingDir, $payloadZip)

        Write-Host "[OK] Payload created: $payloadZip`n" -ForegroundColor Green
        return $payloadZip
    }
    finally {
        # Clean up staging directory
        if (Test-Path $stagingDir) {
            Remove-Item $stagingDir -Recurse -Force
        }
    }
}

# ==============================================================================
# Main Logic
# ==============================================================================

# --- Step 1: Verify all drivers and CAB are signed ---
if ($SkipSignCheck) {
    Write-Host "[INFO] --no_sign_required: skipping Microsoft attestation signature check." -ForegroundColor Yellow
} else {
    Assert-DriversSigned
}

# --- Build payload ---
$PayloadFullPath = (Resolve-Path (New-Payload)).Path

# --- Parse version ---
$Version     = "1.0.0.0"
$ProductName = "Qualcomm USB Kernel Drivers"
$CompanyName = "Qualcomm Technologies, Inc."
$Copyright   = "Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries."
if (Test-Path $Script:VersionFile) {
    $versionContent = Get-Content $Script:VersionFile -Raw
    if ($versionContent -match '#define\s+QCOM_USB_DRIVERS_PRODUCT_VERSION\s+([\d.]+)') {
        $Version = $Matches[1]
        Write-Host "[INFO] Version from header: $Version"
    } else {
        Write-Warning "QCOM_USB_DRIVERS_PRODUCT_VERSION not found in $($Script:VersionFile), using default: $Version"
    }
    if ($versionContent -match '#define\s+QCOM_USB_DRIVERS_PRODUCT_NAME\s+"([^"]+)"') {
        $ProductName = $Matches[1]
    }
    if ($versionContent -match '#define\s+QCOM_USB_DRIVERS_COMPANY_NAME\s+"([^"]+)"') {
        $CompanyName = $Matches[1]
    }
    if ($versionContent -match '#define\s+QCOM_USB_DRIVERS_COPYRIGHT\s+"([^"]+)"') {
        $Copyright = $Matches[1]
    }
} else {
    Write-Warning "[WARNING] Version file not found: $($Script:VersionFile), using default: $Version"
}

# C# source code for the installer
$csharpSource = @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;

[assembly: AssemblyTitle("__PRODUCT_NAME__")]
[assembly: AssemblyDescription("__PRODUCT_NAME__")]
[assembly: AssemblyCompany("__COMPANY_NAME__")]
[assembly: AssemblyProduct("__PRODUCT_NAME__")]
[assembly: AssemblyCopyright("__COPYRIGHT__")]
[assembly: AssemblyVersion("__VERSION__")]
[assembly: AssemblyFileVersion("__VERSION__")]
[assembly: AssemblyInformationalVersion("__VERSION__")]

namespace PayloadInstaller
{
    class Program
    {
        // Fixed install location.
        static readonly string InstallPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            "Qualcomm", "Qualcomm USB Drivers");

        static readonly string QdinstallExe = Path.Combine(InstallPath, "qdinstall.exe");
        static readonly string LogDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
            "Qualcomm", "QUD");

        // Resolved once mode is known; see BuildLogFilePath().
        static string LogFile = null;

        static string BuildLogFilePath(string mode)
        {
            string prefix = (mode == "uninstall") ? "kernel_drivers_uninstall_log" : "kernel_drivers_install_log";
            return Path.Combine(LogDir, prefix + "_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ".txt");
        }

        // Timeout in milliseconds to wait for qpm-cli / qsc-cli before giving up.
        const int LEGACY_TOOL_TIMEOUT_MS = 30000;

        static readonly string[] LegacyPackages = {
            "qualcomm_userspace_driver",
            "qud",
            "qud.slt",
            "qud.internal"
        };

        // ------------------------------------------------------------------ //
        // Logging helpers                                                      //
        // ------------------------------------------------------------------ //

        static StreamWriter _log = null;

        static void OpenLog(string mode)
        {
            try
            {
                LogFile = BuildLogFilePath(mode);
                Directory.CreateDirectory(LogDir);
                _log = new StreamWriter(LogFile, true, Encoding.UTF8);
                _log.AutoFlush = true;
                LogLine("==================================================================");
                LogLine("[LOG] __PRODUCT_NAME__ v__VERSION__");
                LogLine("[LOG] Session started : " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
                LogLine("[LOG] Log file        : " + LogFile);
                LogLine("==================================================================");
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Warning: could not open log file '" + LogFile + "': " + ex.Message);
            }
        }

        static void CloseLog()
        {
            if (_log != null)
            {
                try { _log.Close(); } catch { }
                _log = null;
            }
        }

        static void LogLine(string message)
        {
            if (_log == null) return;
            try { _log.WriteLine("[" + DateTime.Now.ToString("HH:mm:ss") + "] " + message); }
            catch { }
        }

        static void Print(string message)
        {
            Console.WriteLine(message);
            LogLine(message);
        }

        static void PrintError(string message)
        {
            Console.Error.WriteLine(message);
            LogLine("[ERROR] " + message);
        }

        // ------------------------------------------------------------------ //
        // Process execution                                                    //
        // ------------------------------------------------------------------ //

        // timeoutMs <= 0 means wait forever (use only for trusted internal tools).
        static int RunCommand(string fileName, string arguments, int timeoutMs = -1)
        {
            LogLine("[RUN] " + fileName + (arguments != null ? " " + arguments : ""));
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.FileName               = fileName;
                if (arguments != null)
                    psi.Arguments          = arguments;
                psi.UseShellExecute        = false;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError  = true;
                psi.StandardOutputEncoding = Encoding.UTF8;
                psi.StandardErrorEncoding  = Encoding.UTF8;

                Process proc = Process.Start(psi);

                // Drain stdout and stderr asynchronously to prevent buffer deadlock.
                proc.OutputDataReceived += OnOutputData;
                proc.ErrorDataReceived  += OnErrorData;
                proc.BeginOutputReadLine();
                proc.BeginErrorReadLine();

                bool exited;
                if (timeoutMs > 0)
                    exited = proc.WaitForExit(timeoutMs);
                else
                {
                    proc.WaitForExit();
                    exited = true;
                }

                if (!exited)
                {
                    LogLine("[WARN] Process did not exit within " + timeoutMs + " ms  -  killing: " + fileName);
                    Console.Error.WriteLine("Warning: '" + fileName + "' timed out after "
                        + (timeoutMs / 1000) + " s and was terminated.");
                    try { proc.Kill(); } catch { }
                    proc.WaitForExit(5000);
                    return -1;
                }

                // Call WaitForExit() a second time (no-arg) to flush async streams.
                proc.WaitForExit();

                int code = proc.ExitCode;
                LogLine("[EXIT] " + fileName + " exited with code " + code);
                return code;
            }
            catch (Exception ex)
            {
                string msg = "Warning: Failed to run '" + fileName + "': " + ex.Message;
                Console.Error.WriteLine(msg);
                LogLine("[WARN] " + msg);
                return -1;
            }
        }

        // ------------------------------------------------------------------ //
        // Async output handlers                                               //
        // ------------------------------------------------------------------ //

        static void OnOutputData(object sender, DataReceivedEventArgs e)
        {
            if (e.Data != null)
                LogLine("[STDOUT] " + e.Data);
        }

        static void OnErrorData(object sender, DataReceivedEventArgs e)
        {
            if (e.Data != null)
                LogLine("[STDERR] " + e.Data);
        }

        // ------------------------------------------------------------------ //
        // Uninstall                                                            //
        // ------------------------------------------------------------------ //

        // Remove legacy qpm-cli / qsc-cli packages only.
        // Called as the first step of both Install() and Uninstall().
        static void RemoveLegacyPackages()
        {
            LogLine("[STEP] Starting legacy package cleanup");
            foreach (string pkg in LegacyPackages)
            {
                Print("\nUninstalling legacy product: " + pkg + "...");

                int qpmRet = RunCommand("qpm-cli",
                    "--uninstall " + pkg + " --silent --force",
                    LEGACY_TOOL_TIMEOUT_MS);
                LogLine("[INFO] qpm-cli returned: " + qpmRet);

                int qscRet = RunCommand("qsc-cli",
                    "tool uninstall -n " + pkg,
                    LEGACY_TOOL_TIMEOUT_MS);
                LogLine("[INFO] qsc-cli returned: " + qscRet);
            }
        }

        // Remove the currently installed driver (qdinstall -x) and wipe InstallPath.
        // Called as the second step of both Install() and Uninstall().
        static int RemoveCurrentInstallation()
        {
            int result = 0;
            if (File.Exists(QdinstallExe))
            {
                Print("\nRemoving current installation: " + QdinstallExe);
                LogLine("[STEP] Invoking qdinstall.exe -u");
                result = RunCommand(QdinstallExe, "-u");
                if (result != 0)
                    LogLine("[WARN] qdinstall.exe -u returned: " + result + " (continuing)");
            }
            else
            {
                LogLine("[INFO] qdinstall.exe not found at '" + QdinstallExe + "', skipping driver removal");
            }

            if (Directory.Exists(InstallPath))
            {
                LogLine("[STEP] Deleting install directory: " + InstallPath);
                try
                {
                    Directory.Delete(InstallPath, true);
                    LogLine("[INFO] Install directory deleted");
                }
                catch (IOException ex)
                {
                    PrintError("Warning: failed to delete " + InstallPath + ": " + ex.Message);
                }
            }

            return result;
        }

        static int Uninstall()
        {
            // Step 1: uninstall the current driver and wipe the install directory.
            int result = RemoveCurrentInstallation();

            if (result == 0)
                Print("\nUninstall completed successfully.");
            else
                PrintError("\nUninstall failed with exit code: " + result);

            return result;
        }

        // ------------------------------------------------------------------ //
        // Install                                                              //
        // ------------------------------------------------------------------ //

                static int Install()
        {
            // Create InstallPath first so the log file can be opened inside it.
            Directory.CreateDirectory(InstallPath);
            OpenLog("install");

            LogLine("[STEP] Starting installation");
            LogLine("[INFO] Install path: " + InstallPath);

            // Step 1: remove legacy packages installed via qpm-cli / qsc-cli.
            Print("\nStep 1/4: Removing legacy installations...");
            RemoveLegacyPackages();

            // Step 2: uninstall the existing driver and wipe InstallPath.
            Print("\nStep 2/4: Uninstalling current driver installation...");
            LogLine("[STEP] Running self-uninstall before fresh install");
            RemoveCurrentInstallation();
            Print("Uninstall step complete.");

            // Re-create the directory after RemoveCurrentInstallation() wiped it.
            Directory.CreateDirectory(InstallPath);

            try
            {
                // Step 3: extract the fresh payload.
                Print("\nStep 3/4: Extracting payload to: " + InstallPath);
                LogLine("[STEP] Extracting embedded payload");
                Assembly assembly = Assembly.GetExecutingAssembly();
                using (Stream resourceStream = assembly.GetManifestResourceStream("__PAYLOAD_NAME__"))
                {
                    if (resourceStream == null)
                    {
                        PrintError("Error: Embedded payload resource not found.");
                        return 1;
                    }
                    using (ZipArchive archive = new ZipArchive(resourceStream, ZipArchiveMode.Read))
                    {
                        archive.ExtractToDirectory(InstallPath);
                    }
                }
                Print("Extraction complete.");
                LogLine("[INFO] Payload extracted successfully");

                // Step 4: install the fresh driver.
                Print("\nStep 4/4: Installing driver...");
                LogLine("[STEP] Invoking qdinstall.exe -i");
                int result = RunCommand(QdinstallExe, "-i -p \"" + InstallPath + "\"");

                if (result == 0)
                {
                    Print("\nInstall completed successfully.");
                    LogLine("[INFO] Installation finished successfully");
                }
                else
                {
                    PrintError("\nInstall failed with exit code: " + result);
                    PrintError("Install files preserved at: " + InstallPath);
                    LogLine("[INFO] Installation failed  -  files preserved at: " + InstallPath);
                }
                return result;
            }
            catch (Exception ex)
            {
                PrintError("Error: " + ex.Message);
                LogLine("[EXCEPTION] " + ex.ToString());
                return 1;
            }
        }

        static int Main(string[] args)
        {
            // Print banner
            Console.WriteLine("================================================");
            Console.WriteLine(" __PRODUCT_NAME__");
            Console.WriteLine(" Version#: __VERSION__");
            Console.WriteLine(" Built on: __BUILD_TIME__");
            Console.WriteLine(" __COPYRIGHT__");
            Console.WriteLine("================================================");
            Console.WriteLine();

            // Parse arguments
            string mode = "install"; // default (no args)
            if (args.Length > 0)
            {
                string arg = args[0];
                if (arg == "-i" || arg == "--install" || arg == "/install")
                    mode = "install";
                else if (arg == "-u" || arg == "--uninstall" || arg == "/uninstall")
                    mode = "uninstall";
                else if (arg == "-v" || arg == "--version" || arg == "/version")
                    mode = "version";
                else
                {
                    Console.Error.WriteLine("Error: Invalid argument: " + arg);
                    Console.Error.WriteLine("Usage: QUD_Installer.exe [option]");
                    Console.Error.WriteLine("  -i, --install    Install drivers (default)");
                    Console.Error.WriteLine("  -u, --uninstall  Uninstall drivers");
                    Console.Error.WriteLine("  -v, --version    Show version");
                    return 1;
                }
            }

            if (mode == "version")
            {
                Console.WriteLine("Package version: __VERSION__");
                return 0;
            }

            int exitCode;
            if (mode == "uninstall")
            {
                // For a standalone uninstall the log dir already exists;
                // open the log before doing anything.
                Directory.CreateDirectory(LogDir);
                OpenLog("uninstall");
                LogLine("[STEP] Mode: uninstall");
                exitCode = Uninstall();
            }
            else
            {
                LogLine("[STEP] Mode: install");
                exitCode = Install();
            }

            LogLine("[STEP] Session ended : " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  exit=" + exitCode);
            CloseLog();
            Console.WriteLine("\nPlease find the installation logs at: " + LogFile);
            return exitCode;
        }
    }
}
'@

# --- Resolve output exe path ---
# OutputName must be a bare file name. The exe is always written to OutputRoot.
if ($OutputName -match '[\\/]' -or [System.IO.Path]::IsPathRooted($OutputName)) {
    Write-Error "[ERROR] OutputName must be a bare file name: $OutputName"
    exit 1
}
$outputExe = Join-Path $Script:OutputRoot $OutputName

# Write C# source to a temp file in the system temp directory
$sourceFile = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.cs')
$buildTime    = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd HH:mm UTC")
$csharpSource = $csharpSource.Replace("__VERSION__",      $Version)
$csharpSource = $csharpSource.Replace("__PRODUCT_NAME__", $ProductName)
$csharpSource = $csharpSource.Replace("__COMPANY_NAME__", $CompanyName)
$csharpSource = $csharpSource.Replace("__COPYRIGHT__",    $Copyright)
$csharpSource = $csharpSource.Replace("__BUILD_TIME__",   $buildTime)
$csharpSource = $csharpSource.Replace("__PAYLOAD_NAME__", $Script:PayloadName)
Set-Content -Path $sourceFile -Value $csharpSource -Encoding UTF8

# Generate the application manifest (requireAdministrator).
$manifestSource = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="requireAdministrator" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
'@
$manifestFile = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.manifest')
Set-Content -Path $manifestFile -Value $manifestSource -Encoding UTF8

# Locate csc.exe: probe Framework64 first, then Framework, then PATH
$cscPath = $null
foreach ($ver in @("v4.0.30319")) {
    $candidate = "C:\Windows\Microsoft.NET\Framework64\$ver\csc.exe"
    if (Test-Path $candidate) { $cscPath = $candidate; break }
    $candidate = "C:\Windows\Microsoft.NET\Framework\$ver\csc.exe"
    if (Test-Path $candidate) { $cscPath = $candidate; break }
}
if (-not $cscPath) {
    $cscCmd = Get-Command "csc.exe" -ErrorAction SilentlyContinue
    if ($cscCmd) { $cscPath = $cscCmd.Source }
}
if (-not $cscPath) {
    Write-Error "[ERROR] csc.exe not found. Please install .NET Framework 4"
    exit 1
}

# Build the installer
Write-Host "Building installer..."
Write-Host "  Payload: $PayloadFullPath"
Write-Host "  Output:  $outputExe"

$cscArgs = @(
    "/target:exe",
    "/out:$outputExe",
    "/win32manifest:$manifestFile",
    "/resource:$PayloadFullPath,$($Script:PayloadName)",
    "/reference:System.IO.Compression.dll",
    "/reference:System.IO.Compression.FileSystem.dll",
    "/reference:System.dll",
    "/reference:System.Core.dll",
    $sourceFile
)

& $cscPath $cscArgs

$buildExitCode = $LASTEXITCODE
if (Test-Path $sourceFile)   { Remove-Item $sourceFile -Force }
if (Test-Path $manifestFile) { Remove-Item $manifestFile -Force }
if ($buildExitCode -eq 0)
{
    Write-Host "[OK] Build completed successfully: $outputExe" -ForegroundColor Green
    Write-Host ""
} else
{
    Write-Error "[ERROR] Build failed with exit code: $buildExitCode"
    exit $buildExitCode
}
