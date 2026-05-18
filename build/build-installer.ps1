param(
    [string]$PayloadPath,
    [string]$OutputPath
)

# ==============================================================================
# Configuration
# ==============================================================================

$Script:OutputRoot   = Join-Path $PSScriptRoot "target"
$Script:DriversDir   = "Drivers"
$Script:ToolsDir     = "Tools"
$Script:PayloadName  = "payload.zip"
$Script:VersionFile  = Join-Path $PSScriptRoot "..\src\windows\qcversion.h"

# Files to promote from Tools/ to the payload root (alongside Drivers/ and Tools/)
$Script:PromotedTools = @("qdclr.exe", "qdinstall.exe")

# ==============================================================================
# Functions
# ==============================================================================

# Assembles a payload zip from target/Drivers and target/Tools.
# Promoted tools (qdclr.exe, qdinstall.exe) are copied to the zip root.
# Returns the full path to the generated payload zip.
# Payload layout contract (must match expectations in the embedded C# installer):
#   <zip root>/
#     qdinstall.exe   <- promoted from Tools/; used as entry point by C# installer
#     qdclr.exe       <- promoted from Tools/; used by qdinstall.exe -x
#     Drivers/        <- INF files passed to pnputil
#     Tools/          <- remaining tools copied to installPath
function New-Payload {
    Write-Host "========================================"
    Write-Host " Packaging Payload"
    Write-Host "========================================`n"

    $driversSource = Join-Path $Script:OutputRoot $Script:DriversDir
    $toolsSource   = Join-Path $Script:OutputRoot $Script:ToolsDir

    if (-not (Test-Path $driversSource)) {
        Write-Error "Drivers directory not found: $driversSource"
        exit 1
    }
    if (-not (Test-Path $toolsSource)) {
        Write-Error "Tools directory not found: $toolsSource"
        exit 1
    }

    # Create a temp staging directory
    $stagingDir = Join-Path ([System.IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

    try {
        # Copy Drivers/
        $destDrivers = Join-Path $stagingDir $Script:DriversDir
        Copy-Item -Path $driversSource -Destination $destDrivers -Recurse -Force
        Write-Host "[COPY] $Script:DriversDir -> $destDrivers"

        # Copy Tools/
        $destTools = Join-Path $stagingDir $Script:ToolsDir
        Copy-Item -Path $toolsSource -Destination $destTools -Recurse -Force
        Write-Host "[COPY] $Script:ToolsDir -> $destTools"

        # Promote specified tools to staging root and remove from Tools/
        foreach ($toolFile in $Script:PromotedTools) {
            $srcFile = Join-Path $destTools $toolFile
            if (Test-Path $srcFile) {
                Copy-Item -Path $srcFile -Destination $stagingDir -Force
                Remove-Item -Path $srcFile -Force
                Write-Host "[PROMOTE] $toolFile -> payload root"
            } else {
                Write-Warning "Promoted tool not found in Tools: $toolFile"
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

        $zipSize = (Get-Item $payloadZip).Length
        Write-Host "[OK] Payload created: $payloadZip ($zipSize bytes)`n" -ForegroundColor Green

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

# --- Resolve payload ---
if (-not $PayloadPath) {
    $PayloadPath = New-Payload
}

if (-not (Test-Path $PayloadPath)) {
    Write-Error "Payload file not found: $PayloadPath"
    exit 1
}

$PayloadFullPath = (Resolve-Path $PayloadPath).Path

# --- Parse version ---
$Version = "1.0.0.0"
if (Test-Path $Script:VersionFile) {
    $versionContent = Get-Content $Script:VersionFile -Raw
    if ($versionContent -match '#define\s+QCOM_USB_DRIVERS_PRODUCT_VERSION\s+([\d.]+)') {
        $Version = $Matches[1]
        Write-Host "[INFO] Version from header: $Version"
    } else {
        Write-Warning "QCOM_USB_DRIVERS_PRODUCT_VERSION not found in $($Script:VersionFile), using default: $Version"
    }
} else {
    Write-Warning "Version file not found: $($Script:VersionFile), using default: $Version"
}

# C# source code for the installer
$csharpSource = @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using Microsoft.Win32;

[assembly: AssemblyTitle("Qualcomm USB Driver Installer")]
[assembly: AssemblyDescription("Qualcomm USB Driver Installer")]
[assembly: AssemblyCompany("Qualcomm Technologies, Inc.")]
[assembly: AssemblyProduct("Qualcomm USB Kernel Drivers")]
[assembly: AssemblyCopyright("Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.")]
[assembly: AssemblyVersion("__VERSION__")]
[assembly: AssemblyFileVersion("__VERSION__")]
[assembly: AssemblyInformationalVersion("__VERSION__")]

namespace PayloadInstaller
{
    class Program
    {
        static readonly string UninstallRegKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Qualcomm USB Drivers";

        static string[] packages = {
            "qualcomm_userspace_driver",
            "qud",
            "qud.slt",
            "qud.internal"
        };

        static void CopyDirectory(string sourceDir, string destDir)
        {
            Directory.CreateDirectory(destDir);
            foreach (string file in Directory.GetFiles(sourceDir))
            {
                string destFile = Path.Combine(destDir, Path.GetFileName(file));
                File.Copy(file, destFile, true);
            }
            foreach (string dir in Directory.GetDirectories(sourceDir))
            {
                string destSubDir = Path.Combine(destDir, Path.GetFileName(dir));
                CopyDirectory(dir, destSubDir);
            }
        }

        static int ElevateIfNeeded(string[] args)
        {
            var principal = new System.Security.Principal.WindowsPrincipal(
                System.Security.Principal.WindowsIdentity.GetCurrent());
            if (principal.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator))
                return -1; // Already running as admin

            ProcessStartInfo elevate = new ProcessStartInfo();
            elevate.FileName = Assembly.GetExecutingAssembly().Location;
            elevate.Arguments = string.Join(" ", args);
            elevate.UseShellExecute = true;
            elevate.Verb = "runas";
            try
            {
                Process proc = Process.Start(elevate);
                proc.WaitForExit();
                return proc.ExitCode;
            }
            catch (Exception)
            {
                Console.Error.WriteLine("Error: Administrator privileges required.");
                return 1;
            }
        }

        static int RunCommand(string fileName, string arguments)
        {
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.FileName = fileName;
                if (arguments != null)
                    psi.Arguments = arguments;
                psi.UseShellExecute = false;
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

        static int Main(string[] args)
        {
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

            // Self-elevate if not running as administrator
            int elevateResult = ElevateIfNeeded(args);
            if (elevateResult >= 0)
                return elevateResult;

            if (mode == "uninstall")
            {
                // Read install location from registry
                string uninstallPath = null;
                using (var key = Registry.LocalMachine.OpenSubKey(UninstallRegKey))
                {
                    if (key != null)
                        uninstallPath = key.GetValue("InstallLocation") as string;
                }

                if (string.IsNullOrEmpty(uninstallPath))
                {
                    Console.Error.WriteLine("Error: Cannot find installation from registry.");
                    return 1;
                }

                // Run qdinstall.exe -x from the install location
                string qdinstallExe = Path.Combine(uninstallPath, "qdinstall.exe");
                Console.WriteLine("Running uninstaller: " + qdinstallExe);
                int result = RunCommand(qdinstallExe, "-x");

                // Delete install directory on success
                if (result == 0 && !string.IsNullOrEmpty(uninstallPath) && Directory.Exists(uninstallPath))
                {
                    Directory.Delete(uninstallPath, true);
                    Console.WriteLine("Deleted install directory: " + uninstallPath);
                    Console.WriteLine("\nUninstall completed successfully.");
                }
                else if (result != 0)
                {
                    Console.Error.WriteLine("\nUninstall failed with exit code: " + result);
                }
                return result;
            }

            // Install mode
            string installPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Qualcomm", "Qualcomm USB Drivers");

            if (!Directory.Exists(installPath))
                Directory.CreateDirectory(installPath);

            // Extract embedded payload to temp directory
            string tempDir = null;
            try
            {
                tempDir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("B").ToUpper());
                string tempZip = Path.Combine(tempDir, "payload.zip");

                Console.WriteLine("Extracting payload to: " + tempDir);

                Assembly assembly = Assembly.GetExecutingAssembly();
                using (Stream resourceStream = assembly.GetManifestResourceStream("Payload.zip"))
                {
                    if (resourceStream == null)
                    {
                        Console.Error.WriteLine("Error: Embedded payload resource not found.");
                        return 1;
                    }

                    Directory.CreateDirectory(tempDir);
                    using (FileStream fileStream = new FileStream(tempZip, FileMode.Create, FileAccess.Write))
                    {
                        resourceStream.CopyTo(fileStream);
                    }
                }

                ZipFile.ExtractToDirectory(tempZip, tempDir);
                File.Delete(tempZip);
                Console.WriteLine("Extraction complete.");

                // Uninstall legacy packages first (silent to avoid interactive UI)
                foreach (string pkg in packages)
                {
                    Console.WriteLine("\nUninstalling legacy product: " + pkg + "...");
                    RunCommand("qpm-cli", "--uninstall " + pkg + " --silent");
                }

                // Copy files to install path BEFORE registering, so that if the
                // copy fails the registry is never written and no rollback is needed.
                Console.WriteLine("Copying files to: " + installPath);
                try { Directory.Delete(installPath, true); } catch { }
                Directory.CreateDirectory(installPath);
                CopyDirectory(tempDir, installPath);

                // Verify qdinstall.exe is present in the payload root
                // (see payload layout contract in build-installer.ps1: New-Payload)
                string qdinstallPath = Path.Combine(tempDir, "qdinstall.exe");
                if (!File.Exists(qdinstallPath))
                {
                    Console.Error.WriteLine("Error: qdinstall.exe not found in payload root.");
                    Console.Error.WriteLine("       Expected at: " + qdinstallPath);
                    return 1;
                }

                // Run qdinstall to register the installation (files are now in place)
                Console.WriteLine("\nRunning installer...");
                int result = RunCommand(qdinstallPath, "-i -p \"" + installPath + "\"");

                if (result == 0)
                {
                    Console.WriteLine("\nInstall completed successfully.");
                }
                else
                {
                    Console.Error.WriteLine("\nInstall failed with exit code: " + result);
                    // Rollback: remove install directory since registration failed
                    try { Directory.Delete(installPath, true); }
                    catch (Exception ex) { Console.Error.WriteLine("Warning: rollback failed: " + ex.Message); }
                }
                return result;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Error: " + ex.Message);
                return 1;
            }
            finally
            {
                if (tempDir != null && Directory.Exists(tempDir))
                {
                    Directory.Delete(tempDir, true);
                }
            }
        }
    }
}
'@

# --- Resolve output exe path ---
$DefaultExeName = "QUD_Installer.exe"
if (-not $OutputPath) {
    $outputExe = Join-Path $PSScriptRoot $DefaultExeName
} elseif (Test-Path $OutputPath -PathType Container) {
    $outputExe = Join-Path $OutputPath $DefaultExeName
} elseif ($OutputPath -match '\.[^\\\/]+$') {
    # Has extension - treat as full file path
    $outputExe = $OutputPath
    $outputDir = Split-Path $outputExe -Parent
    if ($outputDir -and -not (Test-Path $outputDir)) {
        New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    }
} else {
    # No extension - treat as directory
    if (-not (Test-Path $OutputPath)) {
        New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
    }
    $outputExe = Join-Path $OutputPath $DefaultExeName
}

# Write C# source to a temp file in the system temp directory
$sourceFile = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.cs')

$csharpSource = $csharpSource.Replace("__VERSION__", $Version)
Set-Content -Path $sourceFile -Value $csharpSource -Encoding UTF8

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
    Write-Error "csc.exe not found. Please install .NET Framework 4.x."
    exit 1
}

# Build the installer
Write-Host "Building installer..."
Write-Host "  Payload: $PayloadFullPath"
Write-Host "  Output:  $outputExe"

$cscArgs = @(
    "/target:exe",
    "/out:$outputExe",
    "/resource:$PayloadFullPath,Payload.zip",
    "/reference:System.IO.Compression.dll",
    "/reference:System.IO.Compression.FileSystem.dll",
    $sourceFile
)

& $cscPath $cscArgs

$buildExitCode = $LASTEXITCODE
if (Test-Path $sourceFile) { Remove-Item $sourceFile -Force }
if ($buildExitCode -eq 0) {
    Write-Host "Build successful: $outputExe" -ForegroundColor Green
} else {
    Write-Error "Build failed with exit code: $buildExitCode"
    exit $buildExitCode
}
