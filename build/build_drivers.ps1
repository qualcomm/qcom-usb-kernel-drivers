# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

#Requires -Version 5.0

param(
    [string]$OutputTo
)

# ==============================================================================
# Configuration
# ==============================================================================

$Script:VSWhereExe = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

$Script:SourceRoot = "..\src\windows"  # Windows driver source directory
$Script:OutputRoot = "target"          # Default output directory, use -OutputTo to override
$Script:DriversDir = "drivers"

# Build configuration defaults
$Script:BuildConfiguration = "Release"
$Script:BuildPlatforms = @(
    @{ Platform = "x86";   OutDir = "Win32"; CopyDir = "x86";   OSList = "10_X86" }
    @{ Platform = "x64";   OutDir = "x64";   CopyDir = "amd64"; OSList = "10_X64" }
    @{ Platform = "arm64"; OutDir = "arm64"; CopyDir = "arm64"; OSList = "10_RS4_ARM64,10_RS5_ARM64,10_19H1_ARM64,10_VB_ARM64" }
)
$Script:DriverProjects = @(
    @{ Name = "qcadb";     PackageName = "qcadb";       SolutionPath = $null }
    @{ Name = "filter";    PackageName = "qcusbfilter"; SolutionPath = "$SourceRoot\filter\qcusbfilter.sln" }
    @{ Name = "ndis";      PackageName = "qcusbnet";    SolutionPath = "$SourceRoot\ndis\qcusbnet.sln"}
    @{ Name = "qdss";      PackageName = "qdbusb";      SolutionPath = "$SourceRoot\qdss\qdbusb.sln" }
    @{ Name = "wdfserial"; PackageName = "qcwdfserial"; SolutionPath = "$SourceRoot\wdfserial\qcwdfserial.sln" }
)

# Version header and mappings
$Script:VersionHeaderFile = "$SourceRoot\qcversion.h"
$Script:InfVersionMap = @{
    "qcadb.inf"    = "QCOM_ADB_VERSION"
    "qcfilter.inf" = "QCOM_FILTER_VERSION"
    "qcwwan.inf"   = "QCOM_NET_VERSION"
    "qdbusb.inf"   = "QCOM_QDSS_VERSION"
    "qcwdfser.inf" = "QCOM_WDFSERIAL_VERSION"
    "qcwdfmdm.inf" = "QCOM_WDFSERIAL_VERSION"
}

# Inf2Cat OS targets (built dynamically from BuildPlatforms)
$Script:Inf2CatOSList = ($Script:BuildPlatforms | ForEach-Object { $_.OSList }) -join ","

# Tool paths (auto-detect at runtime)
$Script:MSBuildExe  = $null
$Script:WDKRoot     = $null
$Script:WDKVersion  = $null
$Script:WDKTools = @{
    "inf2cat.exe"   = $null
    "stampinf.exe"  = $null
    "signtool.exe"  = $null
}

# Resolves a path to an absolute path.
function Resolve-ScriptPath {
    param([Parameter(Mandatory)][string]$Path)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $PSScriptRoot $Path
    }
    return $Path
}

# ==============================================================================
# Functions - Dependency Validation
# ==============================================================================

# Locates MSBuild.exe on the system via PATH or vswhere.exe fallback.
function Find-MSBuild {
    $msbuildCmd = Get-Command "msbuild.exe" -ErrorAction SilentlyContinue
    if ($msbuildCmd) {
        Write-Host "[INFO] Found MSBuild on PATH: $($msbuildCmd.Source)"
        return $msbuildCmd.Source
    }

    Write-Host "[INFO] MSBuild not found on PATH. Trying vswhere.exe..."

    if (-not (Test-Path $VSWhereExe)) {
        Write-Host "[WARN] vswhere.exe not found at: $VSWhereExe" -ForegroundColor Yellow
        return $null
    }

    $vsInstallPath = & $VSWhereExe -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if (-not $vsInstallPath) {
        Write-Host "[WARN] vswhere.exe could not find a Visual Studio installation with MSBuild." -ForegroundColor Yellow
        return $null
    }

    Write-Host "[INFO] Visual Studio installation found at: $vsInstallPath"

    $msbuildPath = Join-Path $vsInstallPath "MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $msbuildPath) {
        Write-Host "[INFO] Found MSBuild via vswhere: $msbuildPath"
        return $msbuildPath
    }

    Write-Host "[WARN] MSBuild.exe not found under Visual Studio installation: $vsInstallPath" -ForegroundColor Yellow
    return $null
}

# Locates WDK installation via registry or default path, picks the latest version.
function Find-WDK {
    $regPath = "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"

    foreach ($key in @("KitsRoot10", "KitsRoot81")) {
        $val = (Get-ItemProperty -Path $regPath -Name $key -ErrorAction SilentlyContinue).$key
        if ($val -and (Test-Path $val)) {
            $Script:WDKRoot = $val
            Write-Host "[INFO] WDK root found in registry: $WDKRoot"
            break
        }
    }

    if (-not $WDKRoot) {
        Write-Host "[INFO] WDK not found in registry. Checking default install location..."
        $defaultPath = "C:\Program Files (x86)\Windows Kits\10"
        if (Test-Path $defaultPath) {
            $Script:WDKRoot = $defaultPath
            Write-Host "[INFO] WDK root found at default location: $WDKRoot"
        }
    }

    if (-not $WDKRoot) {
        Write-Host "[WARN] WDK installation not found." -ForegroundColor Yellow
        return $false
    }

    $binDir = Join-Path $WDKRoot "bin"
    if (-not (Test-Path $binDir)) {
        Write-Host "[WARN] WDK bin directory not found: $binDir" -ForegroundColor Yellow
        return $false
    }

    $versions = Get-ChildItem -Path $binDir -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [Version]$_.Name } -Descending
    if (-not $versions) {
        Write-Host "[WARN] No WDK versions found under: $binDir" -ForegroundColor Yellow
        return $false
    }

    $Script:WDKVersion = $versions[0].Name
    Write-Host "[INFO] Latest WDK version: $WDKVersion"
    return $true
}

# Locates a WDK tool by name. Checks PATH first, then falls back to WDKRoot.
function Find-WDKTool {
    param(
        [Parameter(Mandatory)][string]$ToolName
    )

    $cmd = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Host "[INFO] Found $ToolName on PATH: $($cmd.Source)"
        return $cmd.Source
    }

    if (-not $WDKRoot) {
        Write-Host "[ERROR] $ToolName not on PATH and WDKRoot is not set." -ForegroundColor Red
        return $null
    }

    $toolPath = Join-Path $WDKRoot "bin\$WDKVersion\x86\$ToolName"
    if (Test-Path $toolPath) {
        Write-Host "[INFO] Found ${ToolName}: $toolPath"
        return $toolPath
    }

    Write-Host "[ERROR] $ToolName not found at: $toolPath" -ForegroundColor Red
    return $null
}

# ==============================================================================
# Functions - Build
# ==============================================================================

# Builds a single project with MSBuild for a single platform and configuration.
function Build-Project {
    param(
        [Parameter(Mandatory)][string]$SolutionPath,
        [Parameter(Mandatory)][string]$Configuration,
        [Parameter(Mandatory)][string]$Platform
    )

    $SolutionPath = Resolve-ScriptPath $SolutionPath
    if (-not (Test-Path $SolutionPath)) {
        Write-Host "[ERROR] Solution not found: $SolutionPath" -ForegroundColor Red
        return $false
    }

    $slnName = [System.IO.Path]::GetFileName($SolutionPath)
    Write-Host "[BUILD] $slnName | $Configuration | $Platform"
    & $MSBuildExe $SolutionPath `
        /p:Configuration=$Configuration `
        /p:Platform=$Platform `
        /t:Build `
        /m `
        /nologo `
        /verbosity:minimal | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Build failed: $slnName ($Platform)" -ForegroundColor Red
        Write-Host "[ERROR] MSBuild exit code: $LASTEXITCODE" -ForegroundColor Red
        return $false
    }

    Write-Host "[OK] $slnName ($Platform) built successfully.`n" -ForegroundColor Green
    return $true
}

# Builds all configured driver projects for all platforms.
function Build-AllDrivers {
    Write-Host "========================================"
    Write-Host " Building All Drivers"
    Write-Host "========================================`n"

    $successCount = 0

    foreach ($project in $DriverProjects) {
        Write-Host "--- Building $($project.Name) ---" -ForegroundColor Cyan

        if (-not $project.SolutionPath) {
            Write-Host "[INFO] No solution file for $($project.PackageName). Skipped.`n" -ForegroundColor Yellow
            continue
        }

        foreach ($platformInfo in $BuildPlatforms) {
            if (-not (Build-Project `
                -SolutionPath $project.SolutionPath `
                -Configuration $BuildConfiguration `
                -Platform $platformInfo.Platform)) {
                return -1
            }
            $successCount++
        }
    }

    Write-Host "[OK] All drivers built successfully. ($successCount targets)" -ForegroundColor Green
    return $successCount
}

# ==============================================================================
# Functions - Post-Processing
# ==============================================================================

# Parses qcversion.h and returns a hashtable of { versionMacro, versionString }.
function Read-VersionFile {
    $FilePath = $Script:VersionHeaderFile

    if (-not (Test-Path $FilePath)) {
        Write-Host "[ERROR] Version header not found: $FilePath" -ForegroundColor Red
        return $null
    }

    $versions = [ordered]@{}
    $lines = Get-Content -Path $FilePath
    foreach ($line in $lines) {
        if ($line -match '^\s*#define\s+(\S+)\s+(\d+\.\d+\.\d+\.\d+)') {
            $versionMacro  = $Matches[1]
            $versionString = $Matches[2]
            $versions[$versionMacro] = $versionString
        }
    }

    Write-Host "[VERSION] Parsed $($versions.Count) version(s) from: $FilePath"
    foreach ($key in $versions.Keys) {
        Write-Host "[VERSION] $($key.PadRight(32)) = $($versions[$key])"
    }
    return $versions
}

# Updates the [SourceDisksNames] section with subdirectories.
function Update-Inf {
    param(
        [Parameter(Mandatory)][string]$InfPath,
        [Parameter(Mandatory)][string]$DriverName
    )

    if (-not (Test-Path $InfPath)) {
        Write-Host "[ERROR] INF file not found: $InfPath" -ForegroundColor Red
        return $false
    }

    # Build replacement lines dynamically from BuildPlatforms
    $replacement = @()
    foreach ($platformInfo in $BuildPlatforms) {
        if ($replacement.Count -gt 0) { $replacement += "" }
        $replacement += "[SourceDisksNames.$($platformInfo.CopyDir)]"
        $replacement += "1000 = %QcomSrcDisk%,,,\$DriverName\$($platformInfo.CopyDir)"
    }

    $lines = Get-Content -Path $InfPath
    $newLines = @()
    $skipNext = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($skipNext) {
            $skipNext = $false
            continue
        }
        if ($lines[$i] -match '^\[SourceDisksNames\]') {
            $skipNext = $true
            $newLines += $replacement
            continue
        }
        $newLines += $lines[$i]
    }

    Set-Content -Path $InfPath -Value $newLines -Encoding Default
    Write-Host "[UPDATE-INF] Updated: $InfPath ($DriverName)"
    return $true
}

# Runs stampinf.exe on all .inf files in OutputRoot using version info from qcversion.h.
function Run-StampInf {
    Write-Host "========================================"
    Write-Host " Stamping INF Versions (stampinf)"
    Write-Host "========================================`n"

    $versions = Read-VersionFile
    if (-not $versions) {
        Write-Host "[ERROR] Failed to read version file." -ForegroundColor Red
        return $false
    }

    $destinationDir = Join-Path $OutputRoot $Script:DriversDir
    $infFiles = Get-ChildItem -Path $destinationDir -File -Filter "*.inf"
    if (-not $infFiles) {
        Write-Host "[WARN] No .inf files found in: $destinationDir" -ForegroundColor Yellow
        return $true
    }

    foreach ($inf in $infFiles) {
        $versionMacro = $InfVersionMap[$inf.Name]
        if (-not $versionMacro) {
            Write-Host "[WARN] No version mapping for: $($inf.Name)" -ForegroundColor Yellow
            continue
        }

        $version = $versions[$versionMacro]
        if (-not $version) {
            Write-Host "[ERROR] '$versionMacro' not found in version header for: $($inf.Name)" -ForegroundColor Red
            return $false
        }

        Write-Host "[STAMPINF] $($inf.Name) -> v$version ($versionMacro)"
        & $WDKTools["stampinf.exe"] -f "$($inf.FullName)" -d * -v $version 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[ERROR] stampinf failed for $($inf.Name) (exit code: $LASTEXITCODE)" -ForegroundColor Red
            return $false
        }
    }

    Write-Host ""
    Write-Host "[OK] All INF versions stamped successfully." -ForegroundColor Green
    return $true
}

# Runs inf2cat.exe on all .inf files in OutputRoot to generate .cat files.
function Run-Inf2Cat {
    Write-Host "========================================"
    Write-Host " Generating Catalog Files (inf2cat)"
    Write-Host " OS targets: $Inf2CatOSList"
    Write-Host "========================================`n"

    $destinationDir = Join-Path $OutputRoot $Script:DriversDir
    $infFiles = Get-ChildItem -Path $destinationDir -File -Filter "*.inf"
    if (-not $infFiles) {
        Write-Host "[WARN] No .inf files found in: $destinationDir" -ForegroundColor Yellow
        return $false
    }

    foreach ($inf in $infFiles) {
        Write-Host "[INF2CAT] Found: $($inf.Name)"
    }

    & $WDKTools["inf2cat.exe"] /driver:"$destinationDir" /os:$Inf2CatOSList /uselocaltime 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] inf2cat failed with exit code: $LASTEXITCODE" -ForegroundColor Red
        return $false
    }

    Write-Host ""
    Write-Host "[OK] Catalog files generated successfully." -ForegroundColor Green
    return $true
}

# Copies .sys and .pdb files from build output to the output directory.
function Copy-DriverOutputs {
    Write-Host "========================================"
    Write-Host " Copying Driver Outputs"
    Write-Host "========================================`n"

    $hasError = $false
    $driversDir = Join-Path $OutputRoot $Script:DriversDir
    New-Item -ItemType Directory -Path $driversDir -Force | Out-Null

    foreach ($project in $DriverProjects) {
        if ($project.SolutionPath) {
            $projectDir = Resolve-ScriptPath (Split-Path $project.SolutionPath -Parent)
            foreach ($platformInfo in $BuildPlatforms) {
                $packageDir = Join-Path $projectDir "$($platformInfo.OutDir)\$BuildConfiguration\$($project.PackageName)"

                if (-not (Test-Path $packageDir)) {
                    Write-Host "[WARN] Driver package not found: $packageDir" -ForegroundColor Yellow
                    continue
                }

                $destDir = Join-Path $driversDir "$($project.Name)\$($platformInfo.CopyDir)"
                if (-not (Test-Path $destDir)) {
                    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
                }

                $files = Get-ChildItem -Path $packageDir -File |
                    Where-Object { $_.Extension -in ".sys", ".pdb" }
                foreach ($file in $files) {
                    Copy-Item -Path $file.FullName -Destination $destDir -Force
                    Write-Host "[COPY] $($file.Name) -> $destDir"
                }
            }
        }
        else {
            Write-Host "[INFO] Inf-only project: $($project.Name)" -ForegroundColor Yellow
            $projectDir = Resolve-ScriptPath "$SourceRoot\$($project.Name)"
        }

        # Copy .inf files from the project directory to the drivers output directory
        $infFiles = Get-ChildItem -Path $projectDir -File -Filter "*.inf"
        foreach ($file in $infFiles) {
            Copy-Item -Path $file.FullName -Destination $driversDir -Force
            Write-Host "[COPY] $($file.Name) -> $driversDir"

            $destInf = Join-Path $driversDir $file.Name
            if (-not (Update-Inf -InfPath $destInf -DriverName $project.Name)) {
                $hasError = $true
            }
        }
    }

    Write-Host ""
    if ($hasError) {
        Write-Host "[ERROR] Some driver outputs failed to copy or update." -ForegroundColor Red
    } else {
        Write-Host "[OK] Driver outputs copied to: $driversDir" -ForegroundColor Green
    }
    return (-not $hasError)
}

# ==============================================================================
# Main - Entry point: validates dependencies and builds drivers.
# ==============================================================================

function Main {
    Write-Host "========================================"
    Write-Host " QCOM USB Kernel Drivers - Build Script"
    Write-Host "========================================`n"

    if ($OutputTo) {
        $Script:OutputRoot = $OutputTo
    }
    $Script:OutputRoot        = Resolve-ScriptPath $Script:OutputRoot
    $Script:SourceRoot        = Resolve-ScriptPath $Script:SourceRoot
    $Script:VersionHeaderFile = Resolve-ScriptPath $Script:VersionHeaderFile

    # --- Step 1: Locate MSBuild ---
    if (-not $MSBuildExe) {
        $Script:MSBuildExe = Find-MSBuild
    } else {
        Write-Host "[INFO] Using user-specified MSBuild: $MSBuildExe"
    }
    if (-not $MSBuildExe) {
        Write-Host "[ERROR] MSBuild.exe could not be found." -ForegroundColor Red
        Write-Host "[ERROR] Please ensure Visual Studio with C++ build tools is installed." -ForegroundColor Red
        exit 1
    }

    Write-Host "[OK] MSBuild is available at: $MSBuildExe" -ForegroundColor Green
    Write-Host ""

    # --- Step 2: Locate WDK ---
    if (-not (Find-WDK)) {
        Write-Host "[ERROR] WDK could not be found." -ForegroundColor Red
        Write-Host "[ERROR] Please install the Windows Driver Kit (WDK)." -ForegroundColor Red
        exit 1
    }

    Write-Host "[OK] WDK $WDKVersion is available at: $WDKRoot" -ForegroundColor Green
    Write-Host ""

    # --- Step 3: Verify WDK tools ---
    foreach ($toolName in @($WDKTools.Keys)) {
        if (-not $WDKTools[$toolName]) {
            $Script:WDKTools[$toolName] = Find-WDKTool -ToolName $toolName
            if (-not $WDKTools[$toolName]) {
                Write-Host "[ERROR] $toolName is missing. WDK installation may be incomplete." -ForegroundColor Red
                exit 1
            }
        } else {
            Write-Host "[INFO] Using user-specified $toolName"
        }
        Write-Host "[OK] $toolName is available at: $($WDKTools[$toolName])" -ForegroundColor Green
        Write-Host ""
    }

    # --- Step 4: Build all drivers ---
    $buildCount = Build-AllDrivers
    if ($buildCount -lt 0) {
        Write-Host "[ERROR] MSBuild failed. Aborting." -ForegroundColor Red
        exit 1
    }
    if ($buildCount -eq 0) {
        Write-Host "[WARN] No drivers were built. Check build configuration." -ForegroundColor Yellow
        exit 1
    }
    Write-Host ""

    # --- Step 5: Clean output directory ---
    $cleanDir = Join-Path $OutputRoot $DriversDir
    if (-not $OutputTo -and (Test-Path $cleanDir)) {
        Remove-Item -Path $cleanDir -Recurse -Force
        Write-Host "[INFO] Cleaned output directory: $cleanDir"
    }
    Write-Host ""

    # --- Step 6: Copy build outputs and update .inf files ---
    if (-not (Copy-DriverOutputs)) {
        Write-Host "[ERROR] Copy/update step failed. Aborting." -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # --- Step 7: Stamp INF versions ---
    if (-not (Run-StampInf)) {
        Write-Host "[ERROR] Version stamping failed." -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # --- Step 8: Generate catalog files ---
    if (-not (Run-Inf2Cat)) {
        Write-Host "[ERROR] Catalog generation failed." -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    Write-Host "[OK] All build tasks completed successfully." -ForegroundColor Green
    Write-Host "[INFO] Output Location: $(Join-Path $OutputRoot $DriversDir)"
    Write-Host ""
}

Main
