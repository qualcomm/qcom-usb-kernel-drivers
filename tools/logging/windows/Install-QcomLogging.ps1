#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Provisions the always-on 'QcomDrivers' ETW AutoLogger session.

.DESCRIPTION
    Creates (or refreshes) the kernel AutoLogger registry keys under
      HKLM\System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers
    so that ETW starts a circular, bounded (64 MB by default) trace session
    at SERVICE_SYSTEM_START on every subsequent boot. All Qcom WPP provider
    GUIDs listed in QcomTraceProviders.psd1 are enrolled.

    A reboot is required for the kernel to pick up the new AutoLogger; the
    script additionally attempts to start an equivalent live session
    immediately (via 'logman') so logging begins right away.

.PARAMETER Config
    Path to the QcomTraceProviders.psd1 file. Defaults to the copy next to
    this script.

.PARAMETER TraceDir
    Directory where the circular .etl lives. Defaults to %SystemRoot%\Tracing.

.EXAMPLE
    PS> .\Install-QcomLogging.ps1

.EXAMPLE
    PS> .\Install-QcomLogging.ps1 -TraceDir 'D:\QcomTrace'

.NOTES
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
#>
[CmdletBinding()]
param(
    [string] $Config   = (Join-Path $PSScriptRoot 'QcomTraceProviders.psd1'),
    [string] $TraceDir = (Join-Path $env:SystemRoot 'Tracing')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

if (-not (Test-Path $Config)) {
    throw "Configuration file not found: $Config"
}

Write-Step "Loading provider catalogue from $Config"
$cfg = Import-PowerShellDataFile -Path $Config

# Ensure the trace output directory exists.
if (-not (Test-Path $TraceDir)) {
    Write-Step "Creating trace directory $TraceDir"
    New-Item -ItemType Directory -Path $TraceDir -Force | Out-Null
}

$etlPath = Join-Path $TraceDir 'QcomDrivers.etl'

$autoLoggerRoot = 'HKLM:\SYSTEM\CurrentControlSet\Control\WMI\AutoLogger'
$sessionKey     = Join-Path $autoLoggerRoot $cfg.SessionName

Write-Step "Writing AutoLogger session $($cfg.SessionName) -> $etlPath"
if (-not (Test-Path $sessionKey)) {
    New-Item -Path $sessionKey -Force | Out-Null
}

New-ItemProperty -Path $sessionKey -Name 'Start'          -PropertyType DWord      -Value 1                      -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'Guid'           -PropertyType String     -Value $cfg.SessionGuid       -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'LogFileMode'    -PropertyType DWord      -Value $cfg.LogFileMode       -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'FileName'       -PropertyType ExpandString -Value $etlPath             -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'MaxFileSize'    -PropertyType DWord      -Value $cfg.MaxFileSizeMB     -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'BufferSize'     -PropertyType DWord      -Value $cfg.BufferSizeKB      -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'MinimumBuffers' -PropertyType DWord      -Value $cfg.MinBuffers        -Force | Out-Null
New-ItemProperty -Path $sessionKey -Name 'MaximumBuffers' -PropertyType DWord      -Value $cfg.MaxBuffers        -Force | Out-Null

foreach ($p in $cfg.Providers) {
    $guid = $p.Guid
    Write-Step "Enrolling provider $($p.Name) $guid"
    $provKey = Join-Path $sessionKey $guid
    if (-not (Test-Path $provKey)) {
        New-Item -Path $provKey -Force | Out-Null
    }
    New-ItemProperty -Path $provKey -Name 'Enabled'         -PropertyType DWord -Value 1                     -Force | Out-Null
    New-ItemProperty -Path $provKey -Name 'EnableLevel'     -PropertyType DWord -Value $cfg.DefaultLevel     -Force | Out-Null
    # QWORD keyword stored as two DWORDs by AutoLogger; we use the simpler
    # 'MatchAnyKeyword' DWORD form which is honoured on all supported OSes.
    New-ItemProperty -Path $provKey -Name 'MatchAnyKeyword' -PropertyType QWord -Value $cfg.DefaultKeyword   -Force | Out-Null
}

# Attempt to also start a live session right now so we don't have to wait for
# a reboot on first install. Ignore "already running" failures.
Write-Step "Starting live ETW session $($cfg.SessionName) via logman"
$providerList = @()
foreach ($p in $cfg.Providers) {
    $providerList += @('-p', $p.Guid, '0x{0:X}' -f $cfg.DefaultKeyword, $cfg.DefaultLevel)
}

$logmanArgs = @(
    'start', $cfg.SessionName,
    '-o',    $etlPath,
    '-mode', 'Circular',
    '-max',  $cfg.MaxFileSizeMB,
    '-bs',   $cfg.BufferSizeKB,
    '-nb',   $cfg.MinBuffers, $cfg.MaxBuffers,
    '-ets'
)

# logman requires provider args to be interleaved after '-ets'; emit them.
& logman.exe @logmanArgs 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
    Write-Warning "logman start returned $LASTEXITCODE (session may already exist). Continuing."
}

foreach ($p in $cfg.Providers) {
    & logman.exe update trace $cfg.SessionName `
        -p $p.Guid ('0x{0:X}' -f $cfg.DefaultKeyword) $cfg.DefaultLevel `
        -ets 2>&1 | Out-Host
}

Write-Host ""
Write-Host "Done. Reboot once to confirm AutoLogger starts at SERVICE_SYSTEM_START." -ForegroundColor Green
Write-Host "Verify anytime with:  logman query $($cfg.SessionName) -ets" -ForegroundColor Green