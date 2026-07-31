[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceFolder,

    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$destinationRoot = "E:\"
$ownedRelativePaths = @(
    "atmosphere\contents\4200000000BD2300",
    "switch\play-time-control"
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

function Test-InstalledFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,

        [Parameter(Mandatory = $true)]
        [string]$DestinationPath
    )

    foreach ($sourceFile in Get-ChildItem -LiteralPath $SourcePath -File -Recurse) {
        $relativePath = $sourceFile.FullName.Substring($SourcePath.Length).TrimStart("\")
        $destinationFile = Join-Path $DestinationPath $relativePath
        if (-not (Test-Path -LiteralPath $destinationFile -PathType Leaf)) {
            throw "Verification failed; destination file is missing: $destinationFile"
        }

        $sourceHash = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash
        if ($sourceHash -ne $destinationHash) {
            throw "Verification failed; file hash differs: $destinationFile"
        }
    }
}

$sourceRoot = Get-FullDirectoryPath -LiteralPath $SourceFolder
$destinationDrive = Get-PSDrive -Name "E" -PSProvider FileSystem -ErrorAction Stop
if ($destinationDrive.Root -ne $destinationRoot) {
    throw "Drive E: does not resolve to the expected filesystem root $destinationRoot"
}

if ($sourceRoot.StartsWith($destinationRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "SourceFolder must not be on E:, because the old installation is removed before copying."
}

$sourceApp = Join-Path $sourceRoot "switch\play-time-control"
$sourceSysmodule = Join-Path $sourceRoot "atmosphere\contents\4200000000BD2300"
$availableRelativePaths = @()

if (Test-Path -LiteralPath $sourceApp -PathType Container) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceApp "config.json") -PathType Leaf)) {
        throw "Invalid package: switch\play-time-control\config.json is missing."
    }
    $availableRelativePaths += "switch\play-time-control"
}

if (Test-Path -LiteralPath $sourceSysmodule -PathType Container) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceSysmodule "exefs.nsp") -PathType Leaf)) {
        throw "Invalid package: atmosphere\contents\4200000000BD2300\exefs.nsp is missing."
    }
    $availableRelativePaths += "atmosphere\contents\4200000000BD2300"
}

if ($availableRelativePaths.Count -eq 0) {
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
    $confirmation = Read-Host "Type E:\ to confirm removal of the old play-time-control installation"
    if ($confirmation -cne $destinationRoot) {
        throw "Confirmation did not match E:\; no files were changed."
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
        Test-InstalledFiles -SourcePath $sourcePath -DestinationPath $destinationPath
    }
}

if ($WhatIfPreference) {
    Write-Host "WhatIf preview completed. No files were changed."
} else {
    Write-Host "Installation completed and copied files passed SHA-256 verification."
}
