[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceFolder,

    [ValidatePattern('^[A-Za-z]:?$')]
    [string]$Drive = "E",

    [Parameter(Mandatory = $true)]
    [switch]$WipeAll,

    [switch]$Apply,

    [string]$BackupRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $WipeAll.IsPresent) {
    throw "Device Lab qualification preparation requires the explicit -WipeAll switch."
}

$sourceRoot = (Get-Item -LiteralPath $SourceFolder -ErrorAction Stop).FullName
$driveLetter = $Drive.Substring(0, 1).ToUpperInvariant()
$destinationRoot = "${driveLetter}:\"
$destinationDrive = Get-PSDrive -Name $driveLetter -PSProvider FileSystem -ErrorAction Stop
if ($destinationDrive.Root -ne $destinationRoot) {
    throw "Drive ${driveLetter}: does not resolve to the expected filesystem root $destinationRoot"
}
if ($env:SystemDrive -and $env:SystemDrive.Substring(0, 1).ToUpperInvariant() -eq $driveLetter) {
    throw "Refusing to prepare the Windows system drive ${driveLetter}:"
}
if ($sourceRoot.StartsWith($destinationRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "SourceFolder must not be located on the target SD card."
}

$releaseRoot = Join-Path $sourceRoot "playwise"
$labRoot = Join-Path $sourceRoot "playwise-device-lab"
$releaseManifestPath = Join-Path $releaseRoot "switch\playwise\build.json"
$labManifestPath = Join-Path $labRoot "switch\playwise-device-lab\build.json"
foreach ($required in @($releaseManifestPath, $labManifestPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing extracted package manifest: $required"
    }
}
$releaseManifest = Get-Content -LiteralPath $releaseManifestPath -Raw -Encoding utf8 | ConvertFrom-Json
$labManifest = Get-Content -LiteralPath $labManifestPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($releaseManifest.profile -ne "release" -or $labManifest.profile -ne "device-lab") {
    throw "Package profiles must be release and device-lab."
}
if ($releaseManifest.commit -ne $labManifest.commit -or $releaseManifest.release_id -ne $labManifest.release_id) {
    throw "Release and Device Lab packages do not describe the same candidate."
}

$standardFlag = Join-Path $destinationRoot "atmosphere\contents\4200000000BD2300\flags\boot2.flag"
$standardBackup = "$standardFlag.playwise-device-lab-backup"
$labFlag = Join-Path $destinationRoot "atmosphere\contents\4200000000BD23F0\flags\boot2.flag"
$journal = Join-Path $destinationRoot "switch\playwise-device-lab\lab\boot-switch.json"
$journalTemporary = "$journal.tmp"
foreach ($conflict in @($standardBackup, $labFlag, $journal, $journalTemporary)) {
    if (Test-Path -LiteralPath $conflict) {
        throw "Device Lab boot switching is not in the normal state; restore it from the Device Lab NRO first: $conflict"
    }
}
foreach ($flagsDirectory in @(
    (Join-Path $destinationRoot "atmosphere\contents\4200000000BD2300\flags"),
    (Join-Path $destinationRoot "atmosphere\contents\4200000000BD23F0\flags")
)) {
    if (-not (Test-Path -LiteralPath $flagsDirectory -PathType Container)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $flagsDirectory -File -Force) {
        if ($file.Name -ne "boot2.flag") {
            throw "Unknown sysmodule flag blocks automated preparation: $($file.FullName)"
        }
    }
}

if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    $BackupRoot = Join-Path (Split-Path -Parent $sourceRoot) "device-lab-backups"
}
$backupRootFull = [System.IO.Path]::GetFullPath($BackupRoot)
if ($backupRootFull.StartsWith($destinationRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "BackupRoot must be on the host, not on the SD card."
}
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupPath = Join-Path $backupRootFull "playwise-$timestamp-$($releaseManifest.commit.Substring(0, 12))"
$relativePaths = @(
    "switch\playwise",
    "switch\playwise-device-lab",
    "switch\.overlays\playwise.ovl",
    "switch\.overlays\playwise-device-lab.ovl",
    "switch\.overlays\pctc.ovl",
    "atmosphere\contents\4200000000BD2300",
    "atmosphere\contents\4200000000BD23F0"
)

Write-Host "Candidate:      $($releaseManifest.release_id)"
Write-Host "Target SD:      $destinationRoot"
Write-Host "Backup target:  $backupPath"
Write-Host "Mode:           BACKUP then WIPE ALL PlayWise Release and Device Lab data"
Write-Host ""
Write-Host "Existing paths selected for backup:"
foreach ($relative in $relativePaths) {
    $path = Join-Path $destinationRoot $relative
    if (Test-Path -LiteralPath $path) { Write-Host "  $path" }
}

if (-not $Apply.IsPresent) {
    Write-Host ""
    Write-Host "Preview only. No files were changed. Re-run with -Apply after checking the drive and backup target."
    return
}

if ($PSCmdlet.ShouldProcess($backupPath, "Back up all existing PlayWise paths before the clean installation")) {
    New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
    foreach ($relative in $relativePaths) {
        $source = Join-Path $destinationRoot $relative
        if (-not (Test-Path -LiteralPath $source)) { continue }
        $target = Join-Path $backupPath $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target -Recurse -Force
    }
    $backupMetadata = @{
        schema_version = 1
        candidate_release_id = $releaseManifest.release_id
        candidate_commit = $releaseManifest.commit
        source_drive = $destinationRoot
        created_at = (Get-Date).ToString("o")
        paths = $relativePaths
    } | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText((Join-Path $backupPath "backup.json"), $backupMetadata + "`n",
        [System.Text.UTF8Encoding]::new($false))
}

$installer = Join-Path $PSScriptRoot "install_package_to_sd.ps1"
& $installer -SourceFolder $sourceRoot -Drive $driveLetter -Both -CleanAll -Apply

if (-not (Test-Path -LiteralPath $standardFlag -PathType Leaf) -or
    (Get-Item -LiteralPath $standardFlag).Length -ne 0) {
    throw "Post-install verification failed: the standard boot2.flag is missing or non-empty."
}
foreach ($unexpected in @($standardBackup, $labFlag, $journal, $journalTemporary)) {
    if (Test-Path -LiteralPath $unexpected) {
        throw "Post-install verification failed: conflicting boot material exists: $unexpected"
    }
}
Write-Host "Qualification SD preparation completed. The standard sysmodule is enabled; Device Lab remains opt-in."
Write-Host "Recoverable backup: $backupPath"
