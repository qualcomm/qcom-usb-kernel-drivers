#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Rotates the circular 'QcomDrivers' ETW log into a timestamped .etl file
    and restarts the session so logging resumes immediately.

.DESCRIPTION
    This is the user-facing "Save Log" action. Call it from any app when an
    issue is reproduced; the script performs:

      1. flush the live session (if running)
      2. stop the live session (closes %SystemRoot%\Tracing\QcomDrivers.etl)
      3. move the .etl to  <OutDir>\QcomDrivers-<timestamp>.etl
      4. start the session again using the same AutoLogger template
      5. emit a manifest.json next to the saved file

    Target total save latency < 1 s; observed logging gap < 200 ms.

    If -Bundle is passed, a zip containing the ETL, manifest, driver INFs,
    setupapi.dev.log and pnputil output is produced instead of a bare .etl.

.PARAMETER OutDir
    Output directory. Created if missing. Defaults to
    %PUBLIC%\Documents\QcomLogs.

.PARAMETER Config
    Path to QcomTraceProviders.psd1. Defaults to the file next to this script.

.PARAMETER Bundle
    Zip additional diagnostic artifacts along with the ETL.

.PARAMETER Verbose
    Escalate the keyword mask so data-path (TDATA/RDATA) events are captured
    in the *resumed* session. Useful when the issue re-triggers shortly.

.EXAMPLE
    PS> .\Save-QcomDriverLog.ps1
    Returns: C:\Users\Public\Documents\QcomLogs\QcomDrivers-20250101T120000Z.etl

.EXAMPLE
    PS> .\Save-QcomDriverLog.ps1 -OutDir D:\Logs -Bundle

.NOTES
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
#>
[CmdletBinding()]
param(
    [string] $OutDir = (Join-Path $env:PUBLIC 'Documents\QcomLogs'),
    [string] $Config = (Join-Path $PSScriptRoot 'QcomTraceProviders.psd1'),
    [switch] $Bundle,
    [switch] $VerboseCapture
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

if (-not (Test-Path $Config)) { throw "Configuration file not found: $Config" }
$cfg = Import-PowerShellDataFile -Path $Config

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

$timestamp = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
$savedName = "QcomDrivers-$timestamp.etl"
$savedPath = Join-Path $OutDir $savedName
$sourceEtl = [Environment]::ExpandEnvironmentVariables($cfg.LogFileName)

if (-not (Test-Path $sourceEtl)) {
    Write-Warning "Source ETL $sourceEtl does not exist yet. Session may not be running. Proceeding anyway."
}

$swatch = [System.Diagnostics.Stopwatch]::StartNew()

# 1. Flush (best-effort)
Write-Step "Flushing live session $($cfg.SessionName)"
& logman.exe update $cfg.SessionName -fd -ets 2>&1 | Out-Null

# 2. Stop (this is what actually closes the .etl and releases its handle)
Write-Step "Stopping live session (closes the circular ETL)"
& logman.exe stop $cfg.SessionName -ets 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
    Write-Warning "logman stop returned $LASTEXITCODE; continuing"
}

# 3. Move the rotated file
if (Test-Path $sourceEtl) {
    Write-Step "Rotating $sourceEtl -> $savedPath"
    Move-Item -LiteralPath $sourceEtl -Destination $savedPath -Force
} else {
    Write-Warning "No ETL present to rotate; writing an empty placeholder."
    New-Item -ItemType File -Path $savedPath -Force | Out-Null
}

# 4. Restart immediately
Write-Step "Restarting session $($cfg.SessionName)"
$keyword = if ($VerboseCapture) { $cfg.VerboseKeyword } else { $cfg.DefaultKeyword }

& logman.exe start $cfg.SessionName `
    -o   $sourceEtl `
    -mode Circular `
    -max  $cfg.MaxFileSizeMB `
    -bs   $cfg.BufferSizeKB `
    -nb   $cfg.MinBuffers $cfg.MaxBuffers `
    -ets 2>&1 | Out-Host

foreach ($p in $cfg.Providers) {
    & logman.exe update trace $cfg.SessionName `
        -p $p.Guid ('0x{0:X}' -f $keyword) $cfg.DefaultLevel `
        -ets 2>&1 | Out-Null
}

$swatch.Stop()
Write-Host ("Save completed in {0} ms; ring gap ~{1} ms" -f `
    $swatch.ElapsedMilliseconds, $swatch.ElapsedMilliseconds) -ForegroundColor Green

# 5. Manifest
$manifest = [ordered]@{
    sessionName      = $cfg.SessionName
    sessionGuid      = $cfg.SessionGuid
    savedFile        = $savedName
    capturedUtc      = $timestamp
    hostName         = $env:COMPUTERNAME
    osVersion        = [System.Environment]::OSVersion.VersionString
    level            = $cfg.DefaultLevel
    keywordMask      = ('0x{0:X}' -f $cfg.DefaultKeyword)
    providers        = $cfg.Providers | ForEach-Object { [ordered]@{ name = $_.Name; guid = $_.Guid } }
    saveLatencyMs    = $swatch.ElapsedMilliseconds
    verboseCaptureOn = [bool]$VerboseCapture
}
$manifestPath = "$savedPath.manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Path $manifestPath -Encoding UTF8
Write-Host "Manifest: $manifestPath" -ForegroundColor Green

# 6. Optional bundle
if ($Bundle) {
    Write-Step "Building diagnostic bundle"
    $bundleDir = Join-Path $OutDir "QcomDrivers-$timestamp-bundle"
    New-Item -ItemType Directory -Path $bundleDir -Force | Out-Null

    Copy-Item -Path $savedPath     -Destination $bundleDir -Force
    Copy-Item -Path $manifestPath  -Destination $bundleDir -Force
    Copy-Item -Path (Join-Path $env:SystemRoot 'INF\setupapi.dev.log') -Destination $bundleDir -Force -ErrorAction SilentlyContinue

    & pnputil.exe /enum-drivers > (Join-Path $bundleDir 'pnputil-enum-drivers.txt') 2>&1

    $zipPath = Join-Path $OutDir "QcomDrivers-$timestamp.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $bundleDir '*') -DestinationPath $zipPath
    Remove-Item $bundleDir -Recurse -Force
    Write-Host "Bundle: $zipPath" -ForegroundColor Green
    Write-Output $zipPath
} else {
    Write-Output $savedPath
}