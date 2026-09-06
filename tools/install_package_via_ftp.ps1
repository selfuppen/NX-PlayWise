[CmdletBinding(SupportsShouldProcess = $true)]
param(
    # Target Switch FTP endpoint URL (default: ftp://192.168.8.178:5000/)
    [string]$FtpUrl = "ftp://192.168.8.178:5000/",

    # Extracted standard/lab package, or parent folder containing extracted packages
    # If omitted, auto-detects from build\packages
    [string]$SourceFolder,

    # Convenience switch: install PlayWise Device Lab package instead of standard package
    [switch]$Lab,

    # Convenience switch: install BOTH standard Release and Device Lab packages
    [switch]$Both,

    # Generic package type selector: standard (default), lab, or both
    [ValidateSet("standard", "lab", "both")]
    [string]$PackageType = "standard",

    # Clean all PlayWise installations (both standard and lab) before copying
    [switch]$CleanAll,

    # Actually apply changes over FTP (default is dry-run preview)
    [switch]$Apply,

    # Bypass interactive confirmation prompt
    [switch]$Force,

    # FTP socket timeout in seconds
    [ValidateRange(5, 3600)]
    [int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$pythonTool = Join-Path $scriptDir "install_package_via_ftp.py"

if (-not (Test-Path -LiteralPath $pythonTool -PathType Leaf)) {
    throw "FTP installer core script not found: $pythonTool"
}

# Resolve Python executable
$pythonExe = "python"
$cmdArgs = @(
    $pythonTool,
    "--url", $FtpUrl,
    "--timeout", $TimeoutSeconds.ToString()
)

if (-not [string]::IsNullOrWhiteSpace($SourceFolder)) {
    $cmdArgs += @("--source", $SourceFolder)
}

if ($Both.IsPresent) {
    $cmdArgs += "--both"
} elseif ($Lab.IsPresent) {
    $cmdArgs += "--lab"
} elseif ($PackageType -ne "standard") {
    $cmdArgs += @("--package-type", $PackageType)
}

if ($CleanAll.IsPresent) {
    $cmdArgs += "--clean-all"
}

if ($Apply.IsPresent) {
    $cmdArgs += "--apply"
}

if ($Force.IsPresent) {
    $cmdArgs += "--force"
}

& $pythonExe $cmdArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
