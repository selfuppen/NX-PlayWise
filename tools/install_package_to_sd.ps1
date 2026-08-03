[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceFolder,

    # SD card drive; accepts "E", "E:" or "E:\".
    [ValidatePattern('^[A-Za-z](:\\?)?$')]
    [string]$Drive = "E",

    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$destinationDriveLetter = $Drive.Substring(0, 1).ToUpperInvariant()
$destinationRoot = "${destinationDriveLetter}:\"
$ownedRelativePaths = @(
    "atmosphere\contents\4200000000BD2300",
    "switch\play-time-control",
    "switch\.overlays\pctc.ovl"
)

function Get-FullDirectoryPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath
    )

    $item = Get-Item -LiteralPath $LiteralPath -ErrorAction Stop
    if (-not $item.PSIsContainer) {
        throw "SourceFolder must be an extracted package directory: $LiteralPath"
    }

    return $item.FullName.TrimEnd([System.IO.Path]::DirectorySeparatorChar)
}

function Test-InstalledFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceFile,

        [Parameter(Mandatory = $true)]
        [string]$DestinationFile
    )

    if (-not (Test-Path -LiteralPath $DestinationFile -PathType Leaf)) {
        throw "Verification failed; destination file is missing: $DestinationFile"
    }

    $sourceHash = (Get-FileHash -LiteralPath $SourceFile -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $DestinationFile -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Verification failed; file hash differs: $DestinationFile"
    }
}

function Test-InstalledPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,

        [Parameter(Mandatory = $true)]
        [string]$DestinationPath
    )

    $sourceItem = Get-Item -LiteralPath $SourcePath -ErrorAction Stop
    if (-not $sourceItem.PSIsContainer) {
        Test-InstalledFile -SourceFile $sourceItem.FullName -DestinationFile $DestinationPath
        return
    }

    foreach ($sourceFile in Get-ChildItem -LiteralPath $sourceItem.FullName -File -Recurse -Force) {
        $relativePath = $sourceFile.FullName.Substring($sourceItem.FullName.Length).TrimStart("\")
        $destinationFile = Join-Path $DestinationPath $relativePath
        Test-InstalledFile -SourceFile $sourceFile.FullName -DestinationFile $destinationFile
    }

    foreach ($sourceDirectory in Get-ChildItem -LiteralPath $sourceItem.FullName -Directory -Recurse -Force) {
        $relativePath = $sourceDirectory.FullName.Substring($sourceItem.FullName.Length).TrimStart("\")
        $destinationDirectory = Join-Path $DestinationPath $relativePath
        if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
            throw "Verification failed; destination directory is missing: $destinationDirectory"
        }
    }
}

$sourceRoot = Get-FullDirectoryPath -LiteralPath $SourceFolder
$destinationDrive = Get-PSDrive -Name $destinationDriveLetter -PSProvider FileSystem -ErrorAction Stop
if ($destinationDrive.Root -ne $destinationRoot) {
    throw "Drive ${destinationDriveLetter}: does not resolve to the expected filesystem root $destinationRoot"
}

if ($sourceRoot.StartsWith($destinationRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "SourceFolder must not be on ${destinationDriveLetter}:, because the old installation is removed before copying."
}

$sourceApp = Join-Path $sourceRoot "switch\play-time-control"
$sourceSysmodule = Join-Path $sourceRoot "atmosphere\contents\4200000000BD2300"
$sourceOverlay = Join-Path $sourceRoot "switch\.overlays\pctc.ovl"
$availableRelativePaths = @()
$hasPackageCore = $false

if (Test-Path -LiteralPath $sourceApp -PathType Container) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceApp "config.json") -PathType Leaf)) {
        throw "Invalid package: switch\play-time-control\config.json is missing."
    }
    $availableRelativePaths += "switch\play-time-control"
    $hasPackageCore = $true
}

if (Test-Path -LiteralPath $sourceSysmodule -PathType Container) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceSysmodule "exefs.nsp") -PathType Leaf)) {
        throw "Invalid package: atmosphere\contents\4200000000BD2300\exefs.nsp is missing."
    }
    $availableRelativePaths += "atmosphere\contents\4200000000BD2300"
    $hasPackageCore = $true
}

if (Test-Path -LiteralPath $sourceOverlay -PathType Leaf) {
    $availableRelativePaths += "switch\.overlays\pctc.ovl"
}

if (-not $hasPackageCore) {
    throw "SourceFolder is not a play-time-control package directory."
}

Write-Host "Source package: $sourceRoot"
Write-Host "Destination:    $destinationRoot"
Write-Host ""
Write-Host "Old paths to remove if present:"
foreach ($relativePath in $ownedRelativePaths) {
    Write-Host "  $(Join-Path $destinationRoot $relativePath)"
}
Write-Host ""
Write-Host "Package paths to copy:"
foreach ($relativePath in $availableRelativePaths) {
    Write-Host "  $relativePath"
}

if (-not $Apply) {
    Write-Host ""
    Write-Host "Preview only. No files were changed. Re-run with -Apply to install."
    return
}

if (-not $WhatIfPreference) {
    $confirmation = Read-Host "Type $destinationRoot to confirm removal of the old play-time-control installation"
    if ($confirmation -cne $destinationRoot) {
        throw "Confirmation did not match $destinationRoot; no files were changed."
    }
}

foreach ($relativePath in $ownedRelativePaths) {
    $oldPath = Join-Path $destinationRoot $relativePath
    if ((Test-Path -LiteralPath $oldPath) -and $PSCmdlet.ShouldProcess($oldPath, "Remove old installation")) {
        Remove-Item -LiteralPath $oldPath -Recurse -Force
    }
}

if (-not $WhatIfPreference) {
    foreach ($relativePath in $ownedRelativePaths) {
        $oldPath = Join-Path $destinationRoot $relativePath
        if (Test-Path -LiteralPath $oldPath) {
            throw "Old installation path still exists; copying was stopped: $oldPath"
        }
    }
}

foreach ($relativePath in $availableRelativePaths) {
    $sourcePath = Join-Path $sourceRoot $relativePath
    $destinationPath = Join-Path $destinationRoot $relativePath
    $destinationParent = Split-Path -Parent $destinationPath

    if ($PSCmdlet.ShouldProcess($destinationPath, "Copy package from $sourcePath")) {
        New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Recurse -Force
        Test-InstalledPath -SourcePath $sourcePath -DestinationPath $destinationPath
    }
}

if ($WhatIfPreference) {
    Write-Host "WhatIf preview completed. No files were changed."
} else {
    Write-Host "Installation completed and copied files passed SHA-256 verification."
}
