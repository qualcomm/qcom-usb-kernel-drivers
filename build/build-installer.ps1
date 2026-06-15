# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

param(
    [string]$OutputName = "installer.exe",
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Arch = "x64"   # default value (adjust if needed)
)

# ==============================================================================
# Configuration
# ==============================================================================

$Script:OutputRoot   = Join-Path $PSScriptRoot "target"
$Script:PayloadName  = "payload.zip"
$Script:VersionFile  = Join-Path $PSScriptRoot "..\src\windows\qcversion.h"

# Items to include in the payload zip (files or directories under target/).
# Promote: optional list of file names to move to the payload root.
$Script:PayloadItems = @(
    @{ Path = "drivers"; Arch = $null; Promote = $null }
    @{ Path = "tools";   Arch = $null; Promote = @("qdclr.exe", "qdinstall.exe") }
)

# ==============================================================================
# Functions
# ==============================================================================

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
                Copy-Item -Path $archSource -Destination $destDir -Recurse -Force
                Write-Host "[COPY] $($item.Path)/$($item.Arch) -> staging"
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

# --- Build payload ---
$PayloadFullPath = (Resolve-Path (New-Payload)).Path

# --- Parse version ---
$Version     = "1.0.0.0"
$ProductName = "Qualcomm USB Kernel Drivers"
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

[assembly: AssemblyTitle("__PRODUCT_NAME__ Installer")]
[assembly: AssemblyDescription("__PRODUCT_NAME__ Installer")]
[assembly: AssemblyCompany("Qualcomm Technologies, Inc.")]
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

        static readonly string[] LegacyPackages = {
            "qualcomm_userspace_driver",
            "qud",
            "qud.slt",
            "qud.internal"
        };

        static int RunCommand(string fileName, string arguments, bool consoleOutput = true)
        {
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.FileName = fileName;
                if (arguments != null)
                    psi.Arguments = arguments;
                psi.UseShellExecute = false;
                if (!consoleOutput)
                {
                    psi.RedirectStandardOutput = true;
                    psi.RedirectStandardError  = true;
                }
                Process proc = Process.Start(psi);
                proc.WaitForExit();
                return proc.ExitCode;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Warning: Failed to run " + fileName + ": " + ex.Message);
                return -1;
            }
        }

        // Removal of legacy drivers installed by QPM/QSC
        static void UninstallLegacy()
        {
            foreach (string pkg in LegacyPackages)
            {
                Console.WriteLine("\nUninstalling legacy product: " + pkg + "...");
                RunCommand("qpm-cli", "--uninstall " + pkg + " --silent", false);
                RunCommand("qsc-cli", "tool uninstall -n " + pkg, false);
            }
        }

        // Uninstall any previous installation.
        static int Uninstall()
        {
            int result = 0;
            if (File.Exists(QdinstallExe))
            {
                Console.WriteLine("\nRunning uninstaller: " + QdinstallExe);
                result = RunCommand(QdinstallExe, "-u");
            }

            if (Directory.Exists(InstallPath))
            {
                try
                {
                    Directory.Delete(InstallPath, true);
                }
                catch (IOException ex)
                {
                    Console.Error.WriteLine("Warning: failed to delete " + InstallPath + ": " + ex.Message);
                }
            }

            if (result == 0)
                Console.WriteLine("\nUninstall completed successfully.");
            else
                Console.Error.WriteLine("\nUninstall failed with exit code: " + result);

            return result;
        }

        // Install drivers and remove previous installation
        static int Install()
        {
            UninstallLegacy();
            Uninstall();

            try
            {
                Directory.CreateDirectory(InstallPath);

                // Stream-extract embedded payload directly into InstallPath
                Console.WriteLine("\nExtracting payload to: " + InstallPath);
                Assembly assembly = Assembly.GetExecutingAssembly();
                using (Stream resourceStream = assembly.GetManifestResourceStream("__PAYLOAD_NAME__"))
                {
                    if (resourceStream == null)
                    {
                        Console.Error.WriteLine("Error: Embedded payload resource not found.");
                        return 1;
                    }
                    using (ZipArchive archive = new ZipArchive(resourceStream, ZipArchiveMode.Read))
                    {
                        archive.ExtractToDirectory(InstallPath);
                    }
                }
                Console.WriteLine("Extraction complete.");

                Console.WriteLine("\nRunning installer...");
                int result = RunCommand(QdinstallExe, "-i -p \"" + InstallPath + "\"");

                if (result == 0)
                {
                    Console.WriteLine("\nInstall completed successfully.");
                }
                else
                {
                    Console.Error.WriteLine("\nInstall failed with exit code: " + result);
                    Console.Error.WriteLine("Install files preserved at: " + InstallPath);
                }
                return result;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Error: " + ex.Message);
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

            if (mode == "uninstall")
            {
                return Uninstall();
            }

            return Install();
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
