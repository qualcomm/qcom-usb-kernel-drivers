#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Removes the 'QcomDrivers' ETW AutoLogger session and stops any live copy.

.DESCRIPTION
    Removes the registry subtree under
      HKLM\System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers
    and stops any live ETW session by the same name.

.NOTES
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
#>
[CmdletBinding()]
param(
    [string] $Config = (Join-Path $PSScriptRoot 'QcomTraceProviders.psd1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$cfg = Import-PowerShellDataFile -Path $Config

Write-Host "==> Stopping live session $($cfg.SessionName)" -ForegroundColor Cyan
& logman.exe stop $cfg.SessionName -ets 2>&1 | Out-Host

$sessionKey = "HKLM:\SYSTEM\CurrentControlSet\Control\WMI\AutoLogger\$($cfg.SessionName)"
if (Test-Path $sessionKey) {
    Write-Host "==> Removing AutoLogger registry key $sessionKey" -ForegroundColor Cyan
    Remove-Item -Path $sessionKey -Recurse -Force
}

Write-Host "Done." -ForegroundColor Green