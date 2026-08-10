[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceFolder,

    # SD card drive; accepts "E", "E:" or "E:\".
    [ValidatePattern('^[A-Za-z](:\\?)?$')]
    [string]$Drive = "E",

    [switch]$Apply,

    # Clean / Full install switches (removes all existing PlayWise data before copying)
    [switch]$Clean,

    [switch]$Full
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$destinationDriveLetter = $Drive.Substring(0, 1).ToUpperInvariant()
$destinationRoot = "${destinationDriveLetter}:\"
$isFullInstall = $Clean.IsPresent -or $Full.IsPresent

$ownedRelativePaths = @(
    "atmosphere\contents\4200000000BD2300",
    "switch\.overlays\pctc.ovl"
)
if ($isFullInstall) {
    $pathsToRemove = @("switch\playwise") + $ownedRelativePaths
} else {
    $pathsToRemove = $ownedRelativePaths
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)] [string]$LiteralPath,
        [Parameter(Mandatory = $true)] [string]$Text
    )
    [System.IO.File]::WriteAllText($LiteralPath, $Text, [System.Text.UTF8Encoding]::new($false))
}

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
    throw "SourceFolder must not be on ${destinationDriveLetter}:, because package binaries are replaced during installation."
}

$sourceApp = Join-Path $sourceRoot "switch\playwise"
$sourceSysmodule = Join-Path $sourceRoot "atmosphere\contents\4200000000BD2300"
$sourceOverlay = Join-Path $sourceRoot "switch\.overlays\pctc.ovl"
$availableRelativePaths = @()
$hasPackageCore = $false

if (Test-Path -LiteralPath $sourceApp -PathType Container) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceApp "config.json") -PathType Leaf)) {
        throw "Invalid package: switch\playwise\config.json is missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $sourceApp "setup.json") -PathType Leaf)) {
        throw "Invalid package: switch\playwise\setup.json is missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $sourceApp "build.json") -PathType Leaf)) {
        throw "Invalid package: switch\playwise\build.json is missing."
    }
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
    throw "SourceFolder is not a playwise package directory."
}

Write-Host "Source package: $sourceRoot"
Write-Host "Destination:    $destinationRoot"
if ($isFullInstall) {
    Write-Host "Install mode:   Full clean install (removes all existing PlayWise data and copies new package data)"
} else {
    Write-Host "Install mode:   Incremental update (preserves existing config and data)"
}
Write-Host ""
Write-Host "Old paths to remove if present:"
foreach ($relativePath in $pathsToRemove) {
    Write-Host "  $(Join-Path $destinationRoot $relativePath)"
}
Write-Host ""
Write-Host "Package paths to copy:"
if ($isFullInstall) {
    Write-Host "  switch\playwise (full clean install)"
} else {
    Write-Host "  switch\playwise\pctc.nro and build.json (replace); credentials, PIN, rules and runtime data are preserved"
}
foreach ($relativePath in $availableRelativePaths) {
    Write-Host "  $relativePath"
}

if (-not $Apply) {
    Write-Host ""
    Write-Host "Preview only. No files were changed. Re-run with -Apply to install."
    return
}

if (-not $WhatIfPreference) {
    if ($isFullInstall) {
        $confirmation = Read-Host "Type $destinationRoot to confirm FULL CLEAN installation of PlayWise (DELETES switch\playwise and all existing data)"
    } else {
        $confirmation = Read-Host "Type $destinationRoot to confirm replacement of PlayWise binaries"
    }
    if ($confirmation -cne $destinationRoot) {
        throw "Confirmation did not match $destinationRoot; no files were changed."
    }
}

foreach ($relativePath in $pathsToRemove) {
    $oldPath = Join-Path $destinationRoot $relativePath
    if ((Test-Path -LiteralPath $oldPath) -and $PSCmdlet.ShouldProcess($oldPath, "Remove old installation path")) {
        Remove-Item -LiteralPath $oldPath -Recurse -Force
    }
}

if (-not $WhatIfPreference) {
    foreach ($relativePath in $pathsToRemove) {
        $oldPath = Join-Path $destinationRoot $relativePath
        if (Test-Path -LiteralPath $oldPath) {
            throw "Old installation path still exists; copying was stopped: $oldPath"
        }
    }
}

$destinationApp = Join-Path $destinationRoot "switch\playwise"
if ($isFullInstall) {
    if ($PSCmdlet.ShouldProcess($destinationApp, "Full clean install PlayWise package data")) {
        Copy-Item -LiteralPath $sourceApp -Destination $destinationApp -Recurse -Force
        Test-InstalledPath -SourcePath $sourceApp -DestinationPath $destinationApp
    }
} else {
    if ($PSCmdlet.ShouldProcess($destinationApp, "Install Companion and seed missing PlayWise data")) {
        New-Item -ItemType Directory -Path $destinationApp -Force | Out-Null
        foreach ($sourceDirectory in Get-ChildItem -LiteralPath $sourceApp -Directory -Recurse -Force) {
            $relativePath = $sourceDirectory.FullName.Substring($sourceApp.Length).TrimStart("\")
            New-Item -ItemType Directory -Path (Join-Path $destinationApp $relativePath) -Force | Out-Null
        }

        $sourceNro = Join-Path $sourceApp "pctc.nro"
        $destinationNro = Join-Path $destinationApp "pctc.nro"
        Copy-Item -LiteralPath $sourceNro -Destination $destinationNro -Force
        Test-InstalledFile -SourceFile $sourceNro -DestinationFile $destinationNro

        $sourceBuild = Join-Path $sourceApp "build.json"
        $destinationBuild = Join-Path $destinationApp "build.json"
        Copy-Item -LiteralPath $sourceBuild -Destination $destinationBuild -Force
        Test-InstalledFile -SourceFile $sourceBuild -DestinationFile $destinationBuild

        foreach ($seedFile in Get-ChildItem -LiteralPath $sourceApp -File -Filter "*.json" | Where-Object { $_.Name -ne "build.json" }) {
            $destinationSeed = Join-Path $destinationApp $seedFile.Name
            if (-not (Test-Path -LiteralPath $destinationSeed -PathType Leaf)) {
                Copy-Item -LiteralPath $seedFile.FullName -Destination $destinationSeed -Force
                Test-InstalledFile -SourceFile $seedFile.FullName -DestinationFile $destinationSeed
            }
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
