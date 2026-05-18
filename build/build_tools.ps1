#Requires -Version 5.0

param(
    [Parameter(Mandatory)]
    [ValidateSet("x86", "x64", "arm64")]
    [string]$Platform,
    [string]$OutputTo
)

# ==============================================================================
# Configuration
# ==============================================================================

$Script:VSWhereExe = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

$Script:SourceRoot = "..\src\windows"  # Windows source directory, update when build folder moves
$Script:OutputRoot = "target"           # Directory for all output by default
$Script:ToolsDir   = "Tools"

# Build configuration
$Script:BuildConfiguration = "Release"

# Platform mappings: input platform -> MSBuild platform name, output subdirectory template
# OutDir uses {0} as placeholder for the configuration name (e.g., "Release", "Release_EXE")
$Script:PlatformMap = @{
    "x86"   = @{ MSBuild = "x86";   OutDir = "{0}" }
    "x64"   = @{ MSBuild = "x64";   OutDir = "x64\{0}" }
    "arm64" = @{ MSBuild = "ARM64"; OutDir = "ARM64\{0}" }
}

# Tool project definitions
# - Name:               Display name
# - SolutionPath:       Path to .sln file (relative to script root)
# - Configuration:      (Optional) MSBuild configuration; defaults to $BuildConfiguration
# - SupportedPlatforms: Platforms the project can build for
# - OutputFiles:        Files to copy from build output to ToolsDir
$Script:ToolProjects = @(
    @{
        Name               = "qcdev"
        SolutionPath       = "$SourceRoot\tools\qcdev\qcdev.sln"
        SupportedPlatforms = @("x86")
        OutputFiles        = @("qcdev.dll")
    }
    @{
        Name               = "qdcfg"
        SolutionPath       = "$SourceRoot\tools\qdcfg\qdcfg.sln"
        Configuration      = "Release"
        SupportedPlatforms = @("x86")
        OutputFiles        = @("qdcfg.dll")
    }
    @{
        Name               = "qdcfg_exe"
        SolutionPath       = "$SourceRoot\tools\qdcfg\qdcfg.sln"
        Configuration      = "Release_EXE"
        SupportedPlatforms = @("x86")
        OutputFiles        = @("qdcfg.exe")
    }
    @{
        Name               = "qdclr"
        SolutionPath       = "$SourceRoot\tools\qdclr\qdclr.sln"
        SupportedPlatforms = @("x86", "x64", "arm64")
        OutputFiles        = @("qdclr.exe")
    }
    @{
        Name               = "qdinstall"
        SolutionPath       = "$SourceRoot\tools\qdinstall\qdinstall.sln"
        SupportedPlatforms = @("x86", "x64", "arm64")
        OutputFiles        = @("qdinstall.exe")
    }
    @{
        Name               = "wwansvc"
        SolutionPath       = "$SourceRoot\tools\wwansvc\qcmtusvc.sln"
        SupportedPlatforms = @("x86")
        OutputFiles        = @("qcmtusvc.exe")
    }
)

# MSBuild path (auto-detect at runtime)
$Script:MSBuildExe = $null

# ==============================================================================
# Functions
# ==============================================================================

# Resolves a path to an absolute path relative to the script directory.
function Resolve-ScriptPath {
    param([Parameter(Mandatory)][string]$Path)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $PSScriptRoot $Path
    }
    return $Path
}

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

# Builds a single project with MSBuild for a given platform and configuration.
function Build-Project {
    param(
        [Parameter(Mandatory)][string]$SolutionPath,
        [Parameter(Mandatory)][string]$Configuration,
        [Parameter(Mandatory)][string]$MSBuildPlatform
    )

    $SolutionPath = Resolve-ScriptPath $SolutionPath
    if (-not (Test-Path $SolutionPath)) {
        Write-Host "[ERROR] Solution not found: $SolutionPath" -ForegroundColor Red
        return $false
    }

    $slnName = [System.IO.Path]::GetFileName($SolutionPath)
    Write-Host "[BUILD] $slnName | $Configuration | $MSBuildPlatform"
    & $MSBuildExe $SolutionPath `
        /p:Configuration=$Configuration `
        /p:Platform=$MSBuildPlatform `
        /t:Build `
        /m `
        /nologo `
        /verbosity:minimal | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Build failed: $slnName ($MSBuildPlatform)" -ForegroundColor Red
        Write-Host "[ERROR] MSBuild exit code: $LASTEXITCODE" -ForegroundColor Red
        return $false
    }

    Write-Host "[OK] $slnName ($MSBuildPlatform) built successfully.`n" -ForegroundColor Green
    return $true
}

# ==============================================================================
# Main
# ==============================================================================

function Main {
    Write-Host "========================================"
    Write-Host " QCOM USB Tools - Build Script"
    Write-Host " Platform: $Platform"
    Write-Host "========================================`n"

    # --- Resolve output directory ---
    if ($OutputTo) {
        $Script:OutputRoot = $OutputTo
    }
    else {
        $Script:OutputRoot = Resolve-ScriptPath $OutputRoot
    }
    $toolsOutputDir = Join-Path $OutputRoot $ToolsDir

    # --- Locate MSBuild ---
    $Script:MSBuildExe = Find-MSBuild
    if (-not $MSBuildExe) {
        Write-Host "[ERROR] MSBuild.exe could not be found." -ForegroundColor Red
        Write-Host "[ERROR] Please ensure Visual Studio with C++ build tools is installed." -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] MSBuild is available at: $MSBuildExe" -ForegroundColor Green
    Write-Host ""

    # --- Create output directory ---
    if (-not (Test-Path $toolsOutputDir)) {
        New-Item -ItemType Directory -Path $toolsOutputDir -Force | Out-Null
    }

    # --- Build and copy each tool ---
    Write-Host "========================================"
    Write-Host " Building Tools"
    Write-Host "========================================`n"

    $successCount = 0

    foreach ($tool in $ToolProjects) {
        Write-Host "--- $($tool.Name) ---" -ForegroundColor Cyan

        # Determine actual build platform (fallback to x86 if unsupported)
        $actualPlatform = $Platform
        if ($tool.SupportedPlatforms -notcontains $Platform) {
            Write-Host "[WARN] $($tool.Name) does not support '$Platform'. Falling back to 'x86'." -ForegroundColor Yellow
            $actualPlatform = "x86"
        }

        $platformInfo = $PlatformMap[$actualPlatform]

        # Determine build configuration (per-project override or global default)
        $configuration = if ($tool.Configuration) { $tool.Configuration } else { $BuildConfiguration }

        # Build
        if (-not (Build-Project `
            -SolutionPath $tool.SolutionPath `
            -Configuration $configuration `
            -MSBuildPlatform $platformInfo.MSBuild)) {
            Write-Host "[ERROR] Aborting." -ForegroundColor Red
            exit 1
        }

        # Resolve output directory for the built project
        $projectDir = Resolve-ScriptPath (Split-Path $tool.SolutionPath -Parent)
        $outDirRelative = $platformInfo.OutDir -f $configuration
        $buildOutputDir = Join-Path $projectDir $outDirRelative

        # Copy specified output files
        foreach ($fileName in $tool.OutputFiles) {
            $srcFile = Join-Path $buildOutputDir $fileName
            if (-not (Test-Path $srcFile)) {
                Write-Host "[ERROR] Expected output not found: $srcFile" -ForegroundColor Red
                exit 1
            }

            Copy-Item -Path $srcFile -Destination $toolsOutputDir -Force
            Write-Host "[COPY] $fileName -> $toolsOutputDir"
        }

        $successCount++
        Write-Host ""
    }

    # --- Summary ---
    Write-Host "========================================"
    Write-Host " Summary"
    Write-Host "========================================`n"

    Write-Host "[OK] $successCount/$($ToolProjects.Count) tool(s) built successfully." -ForegroundColor Green
    Write-Host "[INFO] Output: $toolsOutputDir"
    Write-Host ""
}

Main
