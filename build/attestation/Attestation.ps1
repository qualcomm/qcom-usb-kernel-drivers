#-------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT license.  See LICENSE file in the project root for full license information.
#-------------------------------------------------------------------------------
<#
.SYNOPSIS
    Script to use Surface Dev Center Manager to Attestation sign a driver package

.PARAMETER ProductName
    Product Name to use for the driver, visible in Hardware Dev Center

.PARAMETER InputPath
    Path to the EV-signed cab file needed for an Attestation-signed driver
    See steps here:
    https://docs.microsoft.com/en-us/windows-hardware/drivers/dashboard/attestation-signing-a-kernel-driver-for-public-release
#>
#Requires -Version 5.0

param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string] $ProductName,

  [Parameter(Mandatory = $true, Position = 1)]
  [ValidateScript( { Test-Path -Path $_ -PathType Leaf })]
  [string] $InputPath
)

###################################################################################################
# Global Error Handler
###################################################################################################
trap {
  Write-Output "----- TRAP ----"
  Write-Output "Unhandled Exception: $($_.Exception.GetType().Name)"
  Write-Output $_.Exception
  $_ | Format-List -Force
}

###################################################################################################
# Globals
###################################################################################################
$global:ErrorActionPreference = "stop"
Set-StrictMode -Version Latest

# All OS signatures to submit for attestation
$Script:ValidSignatures = @(
    "WINDOWS_v100_X64_NI_FULL",
    "WINDOWS_v100_X64_GE_FULL",
    "WINDOWS_v100_ARM64_RS4_FULL",
    "WINDOWS_v100_ARM64_CO_FULL",
    "WINDOWS_v100_ARM64_NI_FULL",
    "WINDOWS_v100_ARM64_GE_FULL",
    "WINDOWS_v100_ARM64_26H1_FULL",
    "WINDOWS_v100_X64_26H1_FULL",
    "WINDOWS_v100_ARM64_25H2_FULL",
    "WINDOWS_v100_X64_25H2_FULL"
)

$Script:SDCMZipUrl     = "https://github.com/microsoft/SDCM/archive/refs/tags/1.2025.326.1.zip"
$Script:SDCMZipVersion = "1.2025.326.1"
$Script:SDCMExtractDir = Join-Path $PSScriptRoot "sdcm-src"
$Script:SDCMSlnPath    = Join-Path $Script:SDCMExtractDir "SDCM-$($Script:SDCMZipVersion)\SurfaceDevCenterManager.sln"
$Script:SDCMReleaseDir = Join-Path $Script:SDCMExtractDir "SDCM-$($Script:SDCMZipVersion)\SurfaceDevCenterManager\bin\Release"
$Script:VSWhereExe     = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

# sdcm.exe lives in its own build output directory alongside all its dependency DLLs
$SCDM = Join-Path $Script:SDCMReleaseDir "sdcm.exe"

###################################################################################################
# Functions
###################################################################################################

# Downloads, builds and sets up sdcm.exe in its build output directory.
function Get-SDCM {
    # Already built - skip
    if (Test-Path $SCDM) {
        $firstLine = Get-Content $SCDM -TotalCount 1 -Encoding UTF8 -ErrorAction SilentlyContinue
        if ($firstLine -notmatch "git-lfs") {
            Write-Output "[SDCM] sdcm.exe already built at: $SCDM"
            return
        }
    }

    # --- Step 1: Download zip ---
    $zipPath = Join-Path $PSScriptRoot "sdcm-src.zip"
    Write-Output "[SDCM] Downloading SDCM $($Script:SDCMZipVersion)..."
    Invoke-WebRequest -Uri $Script:SDCMZipUrl -OutFile $zipPath -UseBasicParsing
    Write-Output "[SDCM] Download complete: $zipPath"

    # --- Step 2: Extract zip ---
    if (Test-Path $Script:SDCMExtractDir) {
        Remove-Item $Script:SDCMExtractDir -Recurse -Force
    }
    Write-Output "[SDCM] Extracting to $($Script:SDCMExtractDir)..."
    Expand-Archive -Path $zipPath -DestinationPath $Script:SDCMExtractDir -Force
    Remove-Item $zipPath -Force
    Write-Output "[SDCM] Extraction complete."

    # --- Step 3: Locate MSBuild via vswhere ---
    Write-Output "[SDCM] Locating MSBuild..."
    $msbuild = $null

    if (Test-Path $Script:VSWhereExe) {
        $vsPath = & $Script:VSWhereExe -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vsPath) {
            $candidate = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) { $msbuild = $candidate }
        }
    }

    if (-not $msbuild) {
        $msbuildCmd = Get-Command "msbuild.exe" -ErrorAction SilentlyContinue
        if ($msbuildCmd) { $msbuild = $msbuildCmd.Source }
    }

    if (-not $msbuild) {
        Write-Output "[ERROR] MSBuild.exe not found. Please install Visual Studio with C++ build tools."
        exit 1
    }
    Write-Output "[SDCM] Using MSBuild: $msbuild"

    # --- Step 4: Restore NuGet packages ---
    Write-Output "[SDCM] Restoring NuGet packages..."
    & $msbuild $Script:SDCMSlnPath /t:Restore /p:Configuration=Release /nologo /verbosity:minimal 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Output "[ERROR] NuGet restore failed (exit code: $LASTEXITCODE)."
        exit 1
    }
    Write-Output "[SDCM] NuGet restore complete."

    # --- Step 5: Build the solution ---
    Write-Output "[SDCM] Building $($Script:SDCMSlnPath)..."
    & $msbuild $Script:SDCMSlnPath /t:Rebuild /p:Configuration=Release /p:Platform=AnyCPU /nologo /verbosity:minimal 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Output "[ERROR] MSBuild failed (exit code: $LASTEXITCODE)."
        exit 1
    }

    if (-not (Test-Path $SCDM)) {
        Write-Output "[ERROR] sdcm.exe not found after build at: $SCDM"
        exit 1
    }

    # --- Step 6: Copy authconfig.json into bin\Release\ so sdcm.exe can find it ---
    $authSrc = Join-Path $PSScriptRoot "authconfig.json"
    $authDst = Join-Path $Script:SDCMReleaseDir "authconfig.json"
    if (Test-Path $authSrc) {
        Copy-Item $authSrc $authDst -Force
        Write-Output "[SDCM] authconfig.json copied to: $authDst"
    } else {
        Write-Output "[ERROR] authconfig.json not found at: $authSrc"
        exit 1
    }

    Write-Output "[SDCM] Build complete. sdcm.exe ready at: $SCDM"
}

# Extracts attested .sys and .cat files from the signed ZIP back into the drivers
# directory, preserving the exact same relative-path structure that was packed into
# the CAB by Make-Cabinet in sign.ps1.
#
# The CAB (and therefore the signed ZIP Microsoft returns) stores files with paths
# rooted at the *parent* of DriversDir, e.g.:
#
#   drivers\qcadb.cat                   <- flat .cat files
#   drivers\filter\amd64\qcusbfilter.sys
#   drivers\filter\arm64\qcusbfilter.sys
#   drivers\ndis\amd64\qcusbnet.sys
#   ...
#
# We locate the "drivers" subtree inside the extracted ZIP, then replay every
# .sys / .cat file back to the identical relative path under $DriversDir.
# Files that exist in the ZIP but have no matching pre-existing destination are
# reported as errors so nothing is silently dropped.
function Expand-SignedDrivers {
    param(
        [Parameter(Mandatory)][string]$SignedZip,
        [Parameter(Mandatory)][string]$DriversDir
    )

    Write-Output "> Extracting signed drivers"

    if (-not (Test-Path $SignedZip)) {
        Write-Output "[ERROR] Signed ZIP not found: $SignedZip"
        exit 1
    }

    # Resolve DriversDir to an absolute path so substring operations are reliable.
    $DriversDir = (Resolve-Path $DriversDir).Path

    # Extract the signed ZIP to a temp directory.
    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N"))
    Write-Output "    * Extracting: $SignedZip -> $tempDir"
    Expand-Archive -Path $SignedZip -DestinationPath $tempDir -Force

    # -----------------------------------------------------------------------
    # Locate the root inside the ZIP that corresponds to DriversDir.
    #
    # Make-Cabinet sets .Set SourceDir=<DriversDir> and
    # .Set DestinationDir=drivers\... so every file in the CAB (and the
    # returned signed ZIP) lives under a "drivers" folder at the ZIP root.
    # We find that anchor folder by looking for the leaf name of DriversDir.
    # -----------------------------------------------------------------------
    $driversDirName = Split-Path $DriversDir -Leaf   # e.g. "drivers"

        # The Microsoft portal wraps the signed content in an extra folder named
    # after the drivers directory, producing a layout like:
    #
    #   drivers\          <- outer wrapper added by the portal
    #     drivers\        <- inner folder from the CAB's DestinationDir
    #       qcadb.cat
    #       filter\amd64\qcusbfilter.sys
    #
    # We need the INNERMOST (deepest) folder named "drivers" as the anchor,
    # not the outermost one, so we sort by FullName length descending and
    # take the last (deepest) match.
    $anchorDir = Get-ChildItem -Path $tempDir -Recurse -Directory |
        Where-Object { $_.Name -eq $driversDirName } |
        Sort-Object { $_.FullName.Length } -Descending |
        Select-Object -First 1

    if (-not $anchorDir) {
        # Fallback: treat the ZIP root itself as the anchor if no sub-folder
        # matches (some portal versions flatten the output).
        Write-Output "    * [WARN] No '$driversDirName' sub-folder found in ZIP; using ZIP root as anchor."
        $anchorDir = Get-Item $tempDir
    }

    $anchorPath = $anchorDir.FullName   # absolute path inside tempDir

    # -----------------------------------------------------------------------
    # Collect every .sys and .cat file under the anchor and copy each one to
    # the identical relative path under $DriversDir.
    # -----------------------------------------------------------------------
    $signedFiles = Get-ChildItem -Path $anchorPath -Recurse -File |
        Where-Object { $_.Extension -in ".sys", ".cat" }

    if ($signedFiles.Count -eq 0) {
        Write-Output "[ERROR] No .sys or .cat files found in signed ZIP under '$($anchorDir.Name)'."
        Remove-Item $tempDir -Recurse -Force
        exit 1
    }

    $copied  = 0
    $missing = 0

    foreach ($file in $signedFiles) {
        # Compute the path of this file relative to the anchor folder.
        # e.g. anchorPath = ...\tempXYZ\drivers
        #      file.FullName = ...\tempXYZ\drivers\filter\amd64\qcusbfilter.sys
        #      relPath       = filter\amd64\qcusbfilter.sys
        $relPath = $file.FullName.Substring($anchorPath.Length).TrimStart('\', '/')

        # Map to the exact same relative path under the real DriversDir.
        $destPath = Join-Path $DriversDir $relPath

        if (-not (Test-Path $destPath)) {
            Write-Output "    * [ERROR] Pre-attestation file not found for: $relPath"
            $missing++
            continue
        }

        Copy-Item $file.FullName $destPath -Force
        Write-Output "    * [ATTESTED] $relPath"
        $copied++
    }

    Remove-Item $tempDir -Recurse -Force

    Write-Output ""
    Write-Output "    * $copied file(s) replaced with attested versions."

    if ($missing -gt 0) {
        Write-Output "[ERROR] $missing attested file(s) had no matching pre-attestation destination."
        exit 1
    }

    Write-Output "[OK] Signed drivers extracted to: $DriversDir"
}

$CreateSubmissionForAttestationJson = @"
{
  "createType": "submission",
  "createSubmission": {
    "name": "$ProductName",
    "type": "initial"
  }
}
"@

$CreateProductForAttestationJson = @"
{
  "createType": "product",
  "createProduct": {
    "productName": "$ProductName",
    "testHarness": "Attestation",
    "announcementDate": "2018-04-01T00:00:00",
    "deviceMetadataIds": null,
    "firmwareVersion": "0",
    "deviceType": "external",
    "isTestSign": false,
    "isFlightSign": false,
    "marketingNames": null,
    "selectedProductTypes": { "windows_v100_RS4": "Unclassified" },
    "requestedSignatures": [ "WINDOWS_v100_X64_RS4_FULL" ],
    "additionalAttributes": null
  }
}
"@

###################################################################################################
# Main
###################################################################################################

Get-SDCM

Write-Output "Attestation Submission"
Write-Output ""

Write-Output "> Create Product"
$SDCM_PID = ""
Write-Output "    * Create JSON"
$json = $CreateProductForAttestationJson | ConvertFrom-Json
$json.createProduct.productName = "$ProductName"
$json.createProduct.announcementDate = (Get-Date).AddDays(7).ToString("s")
$json.createProduct.requestedSignatures = $Script:ValidSignatures
$json | ConvertTo-Json | Out-File -Encoding ASCII -FilePath (Join-Path $Script:SDCMReleaseDir "CreateAttest.json")
    Write-Output "    * Submit"
    $output = & $SCDM -create (Join-Path $Script:SDCMReleaseDir "CreateAttest.json")

if (-not ([string]$output -match "--- Product: (\d+)")) {
  Write-Output "Did not find product ID"
  Write-Output $output
  exit 1
}
$SDCM_PID = $Matches[1]
Write-Output "    * PID: $SDCM_PID"

Write-Output "> Create Submission"
Write-Output "    * Create JSON"
$json = $CreateSubmissionForAttestationJson | ConvertFrom-Json
$json.createSubmission.name = "$ProductName"
$json | ConvertTo-Json | Out-File -Encoding ASCII -FilePath (Join-Path $Script:SDCMReleaseDir "CreateSubmissionAttest.json")
    Write-Output "    * Submit"
    $output = & $SCDM -create (Join-Path $Script:SDCMReleaseDir "CreateSubmissionAttest.json") -productid $SDCM_PID

if (-not ([string]$output -match "---- Submission: (\d+)")) {
  Write-Output "Did not find submission ID"
  Write-Output $output
  exit 1
}
$SDCM_SID = $Matches[1]
Write-Output "    * SID: $SDCM_SID"

Write-Output "> Upload File"
& $SCDM -upload $InputPath -productid $SDCM_PID -submissionid $SDCM_SID

Write-Output "> Commit Submission"
& $SCDM -commit -productid $SDCM_PID -submissionid $SDCM_SID

Write-Output "> Wait for Submission to complete"
Write-Output "    * Dev Center URL: https://developer.microsoft.com/en-us/dashboard/hardware/driver/$SDCM_PID"
Write-Output "    * PID: $SDCM_PID"
Write-Output "    * SID: $SDCM_SID"
& $SCDM -wait -productid $SDCM_PID -submissionid $SDCM_SID

Write-Output "> Download File"
& $SCDM -productid $SDCM_PID -submissionid $SDCM_SID -download "${InputPath}.signed.zip"

# Extract attested files back into the drivers directory.
# Resolve $InputPath first so Split-Path works correctly even when the
# caller passes a relative path that contains ".." segments.
$resolvedInputPath = (Resolve-Path $InputPath).Path
$driversDir = Join-Path (Split-Path $resolvedInputPath -Parent) "drivers"
Expand-SignedDrivers -SignedZip "${resolvedInputPath}.signed.zip" -DriversDir $driversDir

Write-Output "> Done"
Write-Output "    * Attested drivers: $driversDir"
Write-Output "    * Signed ZIP      : ${InputPath}.signed.zip"

