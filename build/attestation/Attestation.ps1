#-------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT license.  See LICENSE file in the project root for full license information.
#-------------------------------------------------------------------------------
<#
.SYNOPSIS
    Script to use Surface Dev Center Manager to Attestation sign a driver package

.PARAMETER ProductName
    Product Name to use for the driver, visible in Hardware Dev Center

.PARAMETER Signatures
    OS Version and Architecture to submit the driver for

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
  [ValidateSet("WINDOWS_v100_X64_RS3_FULL", "WINDOWS_v100_X64_RS4_FULL")]
  [string[]] $Signatures,

  [Parameter(Mandatory = $true, Position = 2)]
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
$SCDM = Join-Path $PSScriptRoot "./sdcm.exe"

###################################################################################################
# Functions
###################################################################################################
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

Write-Output "Attestation Submission"
Write-Output ""

Write-Output "> Create Product"
$SDCM_PID = ""
Write-Output "    * Create JSON"
$json = $CreateProductForAttestationJson | ConvertFrom-Json
$json.createProduct.productName = "$ProductName"
$json.createProduct.announcementDate = (Get-Date).AddDays(7).ToString("s")
$json.createProduct.requestedSignatures = $Signatures
$json | ConvertTo-Json | Out-File -Encoding ASCII -FilePath "CreateAttest.json"
Write-Output "    * Submit"
$output = & $SCDM -create CreateAttest.json

if (-not ([string]$output -match "--- Product: (\d+)")) {
  Write-Output "Did not find product ID"
  Write-Output $output
  return -1
}
$SDCM_PID = $Matches[1]
Write-Output "    * PID: $SDCM_PID"

Write-Output "> Create Submission"
Write-Output "    * Create JSON"
$json = $CreateSubmissionForAttestationJson | ConvertFrom-Json
$json.createSubmission.name = "$ProductName"
$json | ConvertTo-Json | Out-File -Encoding ASCII -FilePath "CreateSubmissionAttest.json"
Write-Output "    * Submit"
$output = & $SCDM -create CreateSubmissionAttest.json -productid $SDCM_PID

if (-not ([string]$output -match "---- Submission: (\d+)")) {
  Write-Output "Did not find submission ID"
  Write-Output $output
  return -1
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
& $SCDM -productid $SDCM_PID -submissionid $SDCM_SID -download $InputPath`.signed`.zip

Write-Output "> Done"
Write-Output "    * Output: $InputPath`.signed`.zip"

