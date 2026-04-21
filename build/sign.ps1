#Requires -Version 5.0

param(
    [switch]$Whql,
    [string]$InputFrom
)

# ==============================================================================
# Configuration
# ==============================================================================

# Default input directory (relative to script root), used when -InputFrom is not specified
$Script:DefaultInputRoot = "target"
$Script:InputSubDir = "Drivers\Windows10"

# Whether to recurse into subdirectories when scanning for signable files
$Script:Recurse = $false

# Supported file extensions for signing
$Script:SignableExtensions = @(".cat", ".exe", ".sys", ".cab")

# --- EV signing (EVSS) ---
$Script:EvSignServerHost    = "SAGA-QUD-1"
$Script:EvSignServerPort    = 3197
$Script:EvSignServerCommand = "SIGN_FILES"   # "SIGN_FILES" (SHA256) or "SIGN_FILES_SHA1"
$Script:EvSignTimeoutMs     = 60000          # TCP read timeout in milliseconds (1 minute)
$Script:EvSignBufferSize    = 1024           # TCP receive buffer size in bytes
$Script:EvSignEncoding      = [System.Text.Encoding]::ASCII

# --- Attestation signing ---
# $Script:AttestationApiUrl = ""

# --- CAB packaging ---
$Script:DdfFileName = "makecab.ddf"
$Script:CabFileName = "drivers.cab"

# ==============================================================================
# Sign Functions
# ==============================================================================

# Signs a single file remotely using the EVSS (EV Signing Server).
function Sign-EvServer {
    param(
        [Parameter(Mandatory)][string]$File
    )

    # Validate parameters
    if (-not $Script:EvSignServerHost -or -not $Script:EvSignServerPort) {
        Write-Host "[ERROR] EVSS server host and port must be specified." -ForegroundColor Red
        return $false
    }

    if (-not (Test-Path $File)) {
        Write-Host "[ERROR] File not found: $File" -ForegroundColor Red
        return $false
    }

    $fileName = Split-Path $File -Leaf
    Write-Host "[EVSS] Signing: $fileName"
    Write-Host "[EVSS] Server: $($Script:EvSignServerHost):$($Script:EvSignServerPort)"

    try {
        # Step 1: Connect to EVSS
        Write-Host "[EVSS] Connecting to server..."
        $tcpClient = New-Object System.Net.Sockets.TcpClient
        $tcpClient.Connect($Script:EvSignServerHost, $Script:EvSignServerPort)
        $stream = $tcpClient.GetStream()
        $stream.ReadTimeout = $Script:EvSignTimeoutMs
        $buffer = New-Object byte[] $Script:EvSignBufferSize

        # Step 2: Request signing path
        Write-Host "[EVSS] Requesting sign path..."
        $sendBytes = $Script:EvSignEncoding.GetBytes("GET_SIGN_PATH")
        $stream.Write($sendBytes, 0, $sendBytes.Length)

        # Read response - UNC path
        $bytesRead = $stream.Read($buffer, 0, $buffer.Length)
        $signPath = $Script:EvSignEncoding.GetString($buffer, 0, $bytesRead).Trim()

        if (-not $signPath) {
            throw "Empty sign path received from server."
        }
        Write-Host "[EVSS] Sign path: $signPath"

        # Step 3: Copy file to the share path
        if (-not (Test-Path $signPath)) {
            throw "Sign path not accessible: $signPath"
        }

        Write-Host "[EVSS] Copying file to server..."
        $destFile = Join-Path $signPath $fileName
        Copy-Item $File $destFile -Force

        # Step 4: Send sign command
        Write-Host "[EVSS] Sending sign command: $($Script:EvSignServerCommand)"
        $sendBytes = $Script:EvSignEncoding.GetBytes($Script:EvSignServerCommand)
        $stream.Write($sendBytes, 0, $sendBytes.Length)

        # Step 5: Wait for "SIGNING_DONE"
        Write-Host "[EVSS] Waiting for signing to complete..."
        $bytesRead = $stream.Read($buffer, 0, $buffer.Length)
        $response = $Script:EvSignEncoding.GetString($buffer, 0, $bytesRead).Trim()

        if ($response -ne "SIGNING_DONE") {
            throw "Unexpected response from server: $response"
        }

        Write-Host "[EVSS] Server reports signing done."

        # Step 6: Copy signed file back to original location
        Write-Host "[EVSS] Copying signed file back..."
        if (Test-Path $destFile) {
            Copy-Item $destFile $File -Force
            Write-Host "[OK] Signed: $fileName" -ForegroundColor Green
        } else {
            throw "Signed file not found on server: $fileName"
        }

        return $true
    }
    catch {
        Write-Host "[ERROR] EV signing failed: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
    finally {
        if ($tcpClient) { $tcpClient.Dispose() }
    }
}

# Signs a single file using attestation signing (future implementation).
function Sign-Attestation {
    param(
        [Parameter(Mandatory)][string]$File
    )

    Write-Host "[ATTESTATION] Signing: $(Split-Path $File -Leaf)"
    Write-Host "[WARN] Attestation signing is not yet implemented." -ForegroundColor Yellow
    return $true
}

# ==============================================================================
# CAB Packaging
# ==============================================================================

# Generates a DDF from SourceDir, runs makecab, and outputs DDF + CAB into OutputDir.
# Returns the CAB file path on success, or $false on failure.
function Make-Cabinet {
    param(
        [Parameter(Mandatory)][string]$SourceDir,   # e.g. target\Drivers\Windows10
        [Parameter(Mandatory)][string]$OutputDir    # e.g. target
    )

    $ddfPath = Join-Path $OutputDir $Script:DdfFileName
    $cabPath = Join-Path $OutputDir $Script:CabFileName

    # --- Generate DDF ---
    if (-not (Test-Path $SourceDir)) {
        Write-Host "[ERROR] Source directory not found: $SourceDir" -ForegroundColor Red
        return $false
    }

    $lines = @(
        ";*** Auto-generated DDF - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ***"
        ""
        ".OPTION EXPLICIT"
        ".Set CabinetNameTemplate=$($Script:CabFileName)"
        ".Set DiskDirectory1=$OutputDir"
        ".Set SourceDir=$SourceDir"
        ".Set CompressionType=MSZIP"
        ".Set Compress=on"
        ".Set Cabinet=on"
        ".Set RptFileName=nul"
        ".Set InfFileName=nul"
        ".Set MaxDiskSize=0"
        ""
    )

    $allFiles = @(Get-ChildItem $SourceDir -Recurse -File)
    if ($allFiles.Count -eq 0) {
        Write-Host "[WARN] No files found in: $SourceDir" -ForegroundColor Yellow
        return $false
    }

    $groups = $allFiles | Group-Object DirectoryName | Sort-Object Name

    $sourceParent = Split-Path $SourceDir -Parent
    if (-not $sourceParent) { $sourceParent = "." }
    $resolvedSourceParent = (Resolve-Path $sourceParent).Path
    $resolvedSourceDir    = (Resolve-Path $SourceDir).Path

    foreach ($group in $groups) {
        $destDir = $group.Name.Substring($resolvedSourceParent.Length + 1)

        $lines += ".Set DestinationDir=$destDir"
        foreach ($file in $group.Group) {
            $relFile = $file.FullName.Substring($resolvedSourceDir.Length + 1)
            $lines += $relFile
        }
        $lines += ""
    }

    Set-Content -Path $ddfPath -Value $lines -Encoding ASCII
    Write-Host "[OK] Generated DDF: $ddfPath ($($allFiles.Count) files)" -ForegroundColor Green

    # --- Run makecab ---
    & makecab /F $ddfPath 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] makecab failed (exit code: $LASTEXITCODE)" -ForegroundColor Red
        return $false
    }
    if (-not (Test-Path $cabPath)) {
        Write-Host "[ERROR] CAB file not found after makecab: $cabPath" -ForegroundColor Red
        return $false
    }
    Write-Host "[OK] Generated CAB: $cabPath ($((Get-Item $cabPath).Length) bytes)" -ForegroundColor Green

    return $cabPath
}

# ==============================================================================
# Main
# ==============================================================================

function Main {
    Write-Host "========================================"
    Write-Host " QCOM USB Drivers - Code Signing"
    Write-Host "========================================`n"

    # --- Resolve input path ---
    if ($InputFrom) {
        $inputPath = $InputFrom
    } else {
        $inputPath = Join-Path $PSScriptRoot $Script:DefaultInputRoot
    }

    if (-not (Test-Path $inputPath)) {
        Write-Host "[ERROR] Input not found: $inputPath" -ForegroundColor Red
        exit 1
    }
    $inputPath = (Resolve-Path $inputPath).Path

    # --- Single file mode ---
    if (Test-Path $inputPath -PathType Leaf) {
        $ext = [IO.Path]::GetExtension($inputPath).ToLower()
        Write-Host "[INFO] Sign file: $inputPath"

        if ($Whql) {
            Write-Host "[INFO] Sign method: Attestation`n"
            if ($ext -ne ".cab") {
                Write-Host "[ERROR] Attestation signing requires .cab file, got: $ext" -ForegroundColor Red
                exit 1
            }
            $signOk = Sign-Attestation -File $inputPath
        }
        else {
            Write-Host "[INFO] Sign method: EV`n"
            if ($ext -notin $Script:SignableExtensions) {
                Write-Host "[ERROR] Unsupported file type: $ext" -ForegroundColor Red
                exit 1
            }
            $signOk = Sign-EvServer -File $inputPath
        }

        Write-Host ""
        if (-not $signOk) {
            Write-Host "[ERROR] Signing failed." -ForegroundColor Red
            exit 1
        }
        Write-Host "[OK] File signed successfully." -ForegroundColor Green
        exit 0
    }

    # --- Batch mode (directory) ---
    $outputDir = $inputPath
    $driverDir = Join-Path $inputPath $Script:InputSubDir

    Write-Host "[INFO] Batch mode"

    if (-not (Test-Path $driverDir)) {
        Write-Host "[ERROR] Driver directory not found: $driverDir" -ForegroundColor Red
        exit 1
    }

    $files = @(Get-ChildItem $driverDir -Recurse:$Script:Recurse -File |
        Where-Object { $_.Extension.ToLower() -in $Script:SignableExtensions })

    if ($files.Count -eq 0) {
        Write-Host "[WARN] No signable files found." -ForegroundColor Yellow
        exit 0
    }
    Write-Host "[INFO] $($files.Count) file(s) found in $driverDir"
    Write-Host ""

    Write-Host "[INFO] Starting EV signing...`n" -ForegroundColor Cyan
    foreach ($file in $files) {
        if (-not (Sign-EvServer -File $file.FullName)) {
            Write-Host "[ERROR] EV signing failed. Aborted" -ForegroundColor Red
            exit 1
        }
        Write-Host ""
    }

    Write-Host "[INFO] Creating CAB from $driverDir`n" -ForegroundColor Cyan
    $cabPath = Make-Cabinet -SourceDir $driverDir -OutputDir $outputDir
    if (-not $cabPath) {
        Write-Host "[ERROR] CAB creation failed." -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    if (-not (Sign-EvServer -File $cabPath)) {
        Write-Host "[ERROR] EV signing failed." -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    if ($Whql) {
        Write-Host "[INFO] Starting attestation signing...`n" -ForegroundColor Cyan
        if (-not (Sign-Attestation -File $cabPath)) {
            Write-Host "[ERROR] Attestation signing failed." -ForegroundColor Red
            exit 1
        }
        Write-Host ""
    }

    Write-Host "[OK] All files signed successfully." -ForegroundColor Green
    exit 0
}

Main
