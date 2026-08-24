[CmdletBinding(SupportsShouldProcess = $true)]
param(
    # The source folder containing the package data.
    # It can point directly to the extracted package (e.g., build\packages\playwise),
    # or to the parent folder containing multiple packages (e.g., build\packages).
    # If the parent is provided, it automatically selects the correct 'playwise' or
    # 'playwise-device-lab' subfolder depending on the -Lab switch.
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceFolder,

    # SD card drive; accepts "E", "E:" or "E:\".
    [ValidatePattern('^[A-Za-z](:\\?)?$')]
    [string]$Drive = "E",

    [switch]$Apply,

    [switch]$Lab,

    # Install BOTH Release and Device Lab packages simultaneously.
    # When specified, -SourceFolder MUST be the parent directory containing both packages.
    [switch]$Both,

    # Clean / Full install switches (removes all existing PlayWise data before copying)
    [switch]$Clean,

    [switch]$Full,

    # Cleans both Release and Device Lab packages simultaneously to avoid conflicts.
    # Removes all data (app paths, sysmodules, overlays) for BOTH editions before copying.
    [switch]$CleanAll
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$destinationDriveLetter = $Drive.Substring(0, 1).ToUpperInvariant()
$destinationRoot = "${destinationDriveLetter}:\"
$isFullInstall = $Clean.IsPresent -or $Full.IsPresent -or $CleanAll.IsPresent

$installConfigs = @()

if ($Both.IsPresent -or -not $Lab.IsPresent) {
    $installConfigs += @{
        AppName = "playwise"
        SysmoduleId = "4200000000BD2300"
        OverlayName = "playwise.ovl"
        DisplayName = "PlayWise"
    }
}
if ($Both.IsPresent -or $Lab.IsPresent) {
    $installConfigs += @{
        AppName = "playwise-device-lab"
        SysmoduleId = "4200000000BD23F0"
        OverlayName = "playwise-device-lab.ovl"
        DisplayName = "PlayWise Device Lab"
    }
}

$allAppPaths = @("switch\playwise", "switch\playwise-device-lab")
$allSysmodulePaths = @("atmosphere\contents\4200000000BD2300", "atmosphere\contents\4200000000BD23F0")
$allOverlayPaths = @("switch\.overlays\playwise.ovl", "switch\.overlays\playwise-device-lab.ovl", "switch\.overlays\pctc.ovl")

if ($CleanAll.IsPresent) {
    $pathsToRemove = $allAppPaths + $allSysmodulePaths + $allOverlayPaths
} elseif ($isFullInstall) {
    $pathsToRemove = @()
    foreach ($config in $installConfigs) {
        $pathsToRemove += "switch\$($config.AppName)"
        $pathsToRemove += "atmosphere\contents\$($config.SysmoduleId)"
        $pathsToRemove += "switch\.overlays\$($config.OverlayName)"
    }
    $pathsToRemove += "switch\.overlays\pctc.ovl"
} else {
    $pathsToRemove = @()
    foreach ($config in $installConfigs) {
        $pathsToRemove += "atmosphere\contents\$($config.SysmoduleId)"
        $pathsToRemove += "switch\.overlays\$($config.OverlayName)"
    }
    $pathsToRemove += "switch\.overlays\pctc.ovl"
}
$pathsToRemove = $pathsToRemove | Select-Object -Unique

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

$sourceRootBase = Get-FullDirectoryPath -LiteralPath $SourceFolder

$tasks = @()

foreach ($config in $installConfigs) {
    $appName = $config.AppName
    $appPath = "switch\$appName"
    $sysmodulePath = "atmosphere\contents\$($config.SysmoduleId)"
    $overlayPath = "switch\.overlays\$($config.OverlayName)"
    
    $sourceRoot = $sourceRootBase
    
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $appPath)) -and (Test-Path -LiteralPath (Join-Path $sourceRoot $appName))) {
        $nestedPath = Join-Path $sourceRoot $appName
        if (Test-Path -LiteralPath (Join-Path $nestedPath $appPath)) {
            $sourceRoot = $nestedPath
            Write-Host "Auto-detected nested package directory for $($config.DisplayName): $sourceRoot"
        }
    }
    
    $sourceApp = Join-Path $sourceRoot $appPath
    $sourceSysmodule = Join-Path $sourceRoot $sysmodulePath
    $sourceOverlay = Join-Path $sourceRoot $overlayPath
    $availableRelativePaths = @()
    $hasPackageCore = $false

    if (Test-Path -LiteralPath $sourceApp -PathType Container) {
        if ($appName -eq "playwise") {
            $defaultFiles = @("config.json", "auth.json", "rules.json", "state.json", "compatibility.json", "setup.json")
            foreach ($defaultFile in $defaultFiles) {
                $defaultPath = Join-Path $sourceApp (Join-Path "defaults" $defaultFile)
                if (-not (Test-Path -LiteralPath $defaultPath -PathType Leaf)) {
                    throw "Invalid package: $appPath\defaults\$defaultFile is missing."
                }
                $mutableSeed = Join-Path $sourceApp $defaultFile
                if (Test-Path -LiteralPath $mutableSeed) {
                    throw "Invalid package: $appPath\$defaultFile would overwrite runtime data."
                }
            }
        }
        if (-not (Test-Path -LiteralPath (Join-Path $sourceApp "build.json") -PathType Leaf)) {
            throw "Invalid package: $appPath\build.json is missing."
        }
        $hasPackageCore = $true
    }

    if (Test-Path -LiteralPath $sourceSysmodule -PathType Container) {
        if (-not (Test-Path -LiteralPath (Join-Path $sourceSysmodule "exefs.nsp") -PathType Leaf)) {
            throw "Invalid package: $sysmodulePath\exefs.nsp is missing."
        }
        $availableRelativePaths += $sysmodulePath
        $hasPackageCore = $true
    }

    if (Test-Path -LiteralPath $sourceOverlay -PathType Leaf) {
        $availableRelativePaths += $overlayPath
    }

    if (-not $hasPackageCore) {
        throw "SourceFolder is not a valid playwise package directory for $appName."
    }
    
    $config.SourceRoot = $sourceRoot
    $config.SourceApp = $sourceApp
    $config.AvailableRelativePaths = $availableRelativePaths
    $config.AppPath = $appPath
    $tasks += $config
}

$destinationDrive = Get-PSDrive -Name $destinationDriveLetter -PSProvider FileSystem -ErrorAction Stop
if ($destinationDrive.Root -ne $destinationRoot) {
    throw "Drive ${destinationDriveLetter}: does not resolve to the expected filesystem root $destinationRoot"
}

foreach ($task in $tasks) {
    if ($task.SourceRoot.StartsWith($destinationRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "SourceFolder must not be on ${destinationDriveLetter}:, because package binaries are replaced during installation."
    }
}

if ($Both.IsPresent) {
    Write-Host "Source package: $sourceRootBase (Dual installation)"
} else {
    Write-Host "Source package: $($tasks[0].SourceRoot)"
}
Write-Host "Destination:    $destinationRoot"
if ($CleanAll.IsPresent) {
    Write-Host "Install mode:   Full clean install (Clean All Lab & Release)"
} elseif ($isFullInstall) {
    Write-Host "Install mode:   Full clean install"
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

foreach ($task in $tasks) {
    if ($isFullInstall) {
        Write-Host "  $($task.AppPath) (full clean install)"
    } else {
        Write-Host "  $($task.AppPath) package assets (merge/replace); credentials, PIN, rules and runtime data are preserved"
    }
    foreach ($relativePath in $task.AvailableRelativePaths) {
        Write-Host "  $relativePath"
    }
}

if (-not $Apply) {
    Write-Host ""
    Write-Host "Preview only. No files were changed. Re-run with -Apply to install."
    return
}

if (-not $WhatIfPreference) {
    $displayNames = ($tasks | ForEach-Object { $_.DisplayName }) -join " and "
    if ($CleanAll.IsPresent) {
        $confirmation = Read-Host "Type $destinationDriveLetter to confirm FULL CLEAN installation of $displayNames (DELETES ALL PlayWise Release and Lab data)"
    } elseif ($isFullInstall) {
        $appPathsStr = ($tasks | ForEach-Object { $_.AppPath }) -join " and "
        $confirmation = Read-Host "Type $destinationDriveLetter to confirm FULL CLEAN installation of $displayNames (DELETES $appPathsStr and all existing data)"
    } else {
        $confirmation = Read-Host "Type $destinationDriveLetter to confirm replacement of $displayNames binaries"
    }
    if ($confirmation -cne $destinationDriveLetter) {
        throw "Confirmation did not match drive letter $destinationDriveLetter; no files were changed."
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

foreach ($task in $tasks) {
    $destinationApp = Join-Path $destinationRoot $task.AppPath
    if ($isFullInstall) {
        if ($PSCmdlet.ShouldProcess($destinationApp, "Full clean install $($task.DisplayName) package data")) {
            New-Item -ItemType Directory -Path $destinationApp -Force | Out-Null
            foreach ($sourceChild in Get-ChildItem -LiteralPath $task.SourceApp -Force) {
                Copy-Item -LiteralPath $sourceChild.FullName -Destination $destinationApp -Recurse -Force
            }
            Test-InstalledPath -SourcePath $task.SourceApp -DestinationPath $destinationApp
        }
    } else {
        if ($PSCmdlet.ShouldProcess($destinationApp, "Merge overwrite-safe $($task.DisplayName) package assets")) {
            New-Item -ItemType Directory -Path $destinationApp -Force | Out-Null
            foreach ($sourceDirectory in Get-ChildItem -LiteralPath $task.SourceApp -Directory -Recurse -Force) {
                $relativePath = $sourceDirectory.FullName.Substring($task.SourceApp.Length).TrimStart("\")
                New-Item -ItemType Directory -Path (Join-Path $destinationApp $relativePath) -Force | Out-Null
            }

            foreach ($sourceFile in Get-ChildItem -LiteralPath $task.SourceApp -File -Recurse -Force) {
                $relativePath = $sourceFile.FullName.Substring($task.SourceApp.Length).TrimStart("\")
                $destinationFile = Join-Path $destinationApp $relativePath
                Copy-Item -LiteralPath $sourceFile.FullName -Destination $destinationFile -Force
                Test-InstalledFile -SourceFile $sourceFile.FullName -DestinationFile $destinationFile
            }
        }
    }

    foreach ($relativePath in $task.AvailableRelativePaths) {
        $sourcePath = Join-Path $task.SourceRoot $relativePath
        $destinationPath = Join-Path $destinationRoot $relativePath
        $destinationParent = Split-Path -Parent $destinationPath

        if ($PSCmdlet.ShouldProcess($destinationPath, "Copy package from $sourcePath")) {
            New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Recurse -Force
            Test-InstalledPath -SourcePath $sourcePath -DestinationPath $destinationPath
        }
    }
}

if ($WhatIfPreference) {
    Write-Host "WhatIf preview completed. No files were changed."
} else {
    Write-Host "Installation completed and copied files passed SHA-256 verification."
}
