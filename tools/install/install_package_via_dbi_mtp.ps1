[CmdletBinding(SupportsShouldProcess = $true)]
param(
    # Extracted standard package, or a parent folder containing the extracted
    # build/packages/playwise directory.
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceFolder,

    # Optional exact Windows Shell name of the DBI MTP device. When omitted,
    # the script requires exactly one portable device exposing -StorageName.
    [string]$DeviceName,

    # DBI's raw SD-card view. Do not use "MicroSD install": that endpoint is
    # for NSP/XCI installation and is not an SD filesystem root.
    [ValidateNotNullOrEmpty()]
    [string]$StorageName = "SD Card",

    [ValidateRange(10, 3600)]
    [int]$TimeoutSeconds = 180,

    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$appPath = "switch\playwise"
$contentPath = "atmosphere\contents\4200000000BD2300"
$requiredDefaults = @("config.json", "auth.json", "rules.json", "state.json", "compatibility.json", "setup.json")
$copyFlags = 0x414 # FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI

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

function Get-Sha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath
    )

    $stream = [System.IO.File]::OpenRead($LiteralPath)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha256.ComputeHash($stream)
        return ([System.BitConverter]::ToString($bytes)).Replace('-', '')
    } finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Resolve-StandardPackageRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath
    )

    $root = Get-FullDirectoryPath -LiteralPath $LiteralPath
    if (Test-Path -LiteralPath (Join-Path $root $script:appPath) -PathType Container) {
        return $root
    }

    $nested = Join-Path $root "playwise"
    if (Test-Path -LiteralPath (Join-Path $nested $script:appPath) -PathType Container) {
        Write-Host "Auto-detected extracted standard package: $nested"
        return (Get-FullDirectoryPath -LiteralPath $nested)
    }

    throw "SourceFolder is not an extracted PlayWise standard package (expected $appPath)."
}

function Assert-StandardPackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageRoot
    )

    $appRoot = Join-Path $PackageRoot $script:appPath
    foreach ($defaultFile in $script:requiredDefaults) {
        $defaultPath = Join-Path $appRoot (Join-Path "defaults" $defaultFile)
        if (-not (Test-Path -LiteralPath $defaultPath -PathType Leaf)) {
            throw "Invalid package: $appPath\defaults\$defaultFile is missing."
        }
        if (Test-Path -LiteralPath (Join-Path $appRoot $defaultFile)) {
            throw "Invalid package: $appPath\$defaultFile would overwrite runtime data."
        }
    }
    foreach ($forbiddenFile in @("credentials.json", "capabilities.json")) {
        if (Test-Path -LiteralPath (Join-Path $appRoot $forbiddenFile)) {
            throw "Invalid package: $appPath\$forbiddenFile must not be installed."
        }
    }

    $requiredFiles = @(
        "$appPath\build.json",
        "$appPath\package-artifacts.json",
        "$appPath\pctc.nro",
        "switch\.overlays\playwise.ovl",
        "$contentPath\exefs.nsp",
        "$contentPath\flags\boot2.flag"
    )
    foreach ($relativePath in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot $relativePath) -PathType Leaf)) {
            throw "Invalid package: $relativePath is missing."
        }
    }

    $allowedFiles = @("switch\.overlays\playwise.ovl")
    foreach ($file in (Get-ChildItem -LiteralPath $PackageRoot -File -Recurse -Force)) {
        $relativePath = $file.FullName.Substring($PackageRoot.Length).TrimStart('\')
        $allowed = $relativePath.StartsWith("$appPath\", [System.StringComparison]::OrdinalIgnoreCase) -or
            $relativePath.StartsWith("$contentPath\", [System.StringComparison]::OrdinalIgnoreCase) -or
            $allowedFiles -contains $relativePath
        if (-not $allowed) {
            throw "Invalid package: unexpected file outside the standard runtime paths: $relativePath"
        }
    }

    $bootFlag = Get-Item -LiteralPath (Join-Path $PackageRoot "$contentPath\flags\boot2.flag")
    if ($bootFlag.Length -ne 0) {
        throw "Invalid package: the standard boot2.flag must be empty."
    }

    $build = Get-Content -LiteralPath (Join-Path $appRoot "build.json") -Raw -Encoding utf8 | ConvertFrom-Json
    if ($build.profile -ne "release" -or [string]::IsNullOrWhiteSpace([string]$build.release_id)) {
        throw "Invalid package: build.json must identify a release build."
    }

    $artifactManifest = Get-Content -LiteralPath (Join-Path $appRoot "package-artifacts.json") -Raw -Encoding utf8 | ConvertFrom-Json
    if ($artifactManifest.schema_version -ne 1 -or $artifactManifest.release_id -ne $build.release_id) {
        throw "Invalid package: package-artifacts.json does not match build.json."
    }
    $expectedArtifacts = @(
        "switch/playwise/pctc.nro",
        "switch/.overlays/playwise.ovl",
        "atmosphere/contents/4200000000BD2300/exefs.nsp"
    )
    $actualArtifacts = @($artifactManifest.artifacts.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actualArtifacts.Count -ne $expectedArtifacts.Count -or
        @($expectedArtifacts | Where-Object { $actualArtifacts -notcontains $_ }).Count -ne 0) {
        throw "Invalid package: package-artifacts.json must list exactly the three release components."
    }
    foreach ($property in $artifactManifest.artifacts.PSObject.Properties) {
        $relativePath = $property.Name.Replace('/', '\')
        $sourceFile = Join-Path $PackageRoot $relativePath
        if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
            throw "Invalid package: manifest artifact is missing: $relativePath"
        }
        $sourceItem = Get-Item -LiteralPath $sourceFile
        $sourceHash = (Get-Sha256 -LiteralPath $sourceFile).ToLowerInvariant()
        if ($sourceItem.Length -ne [long]$property.Value.size -or $sourceHash -ne ([string]$property.Value.sha256).ToLowerInvariant()) {
            throw "Invalid package: manifest artifact differs: $relativePath"
        }
    }

    return $build.release_id
}

function Get-ShellItems {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Folder
    )
    return @($Folder.Items())
}

function Get-ShellChild {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Folder,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    foreach ($item in (Get-ShellItems -Folder $Folder)) {
        if ([string]::Equals([string]$item.Name, $Name, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $item
        }
    }
    return $null
}

function Get-ChildFolder {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Folder,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $item = Get-ShellChild -Folder $Folder -Name $Name
    if ($null -eq $item -or -not $item.IsFolder) {
        return $null
    }
    return $item.GetFolder
}

function Get-PortableDeviceCandidates {
    param(
        [Parameter(Mandatory = $true)]
        [object]$ThisPc,

        [Parameter(Mandatory = $true)]
        [string]$RawStorageName
    )

    $candidates = @()
    foreach ($item in (Get-ShellItems -Folder $ThisPc)) {
        if (-not $item.IsFolder -or $item.IsFileSystem) {
            continue
        }
        try {
            $deviceFolder = $item.GetFolder
            $storageFolder = Get-ChildFolder -Folder $deviceFolder -Name $RawStorageName
            if ($null -ne $storageFolder) {
                $candidates += [pscustomobject]@{
                    Name = [string]$item.Name
                    DeviceFolder = $deviceFolder
                    StorageFolder = $storageFolder
                }
            }
        } catch {
            # Some virtual This PC entries do not expose an enumerable folder.
        }
    }
    return @($candidates)
}

function Resolve-DbiStorage {
    param(
        [Parameter(Mandatory = $true)]
        [object]$ThisPc,

        [string]$RequestedDeviceName,

        [Parameter(Mandatory = $true)]
        [string]$RawStorageName
    )

    $candidates = Get-PortableDeviceCandidates -ThisPc $ThisPc -RawStorageName $RawStorageName
    if (-not [string]::IsNullOrWhiteSpace($RequestedDeviceName)) {
        $candidates = @($candidates | Where-Object {
            [string]::Equals($_.Name, $RequestedDeviceName, [System.StringComparison]::OrdinalIgnoreCase)
        })
    }

    if ($candidates.Count -eq 0) {
        $portableNames = @(
            Get-ShellItems -Folder $ThisPc |
                Where-Object { $_.IsFolder -and -not $_.IsFileSystem } |
                ForEach-Object { [string]$_.Name }
        )
        $available = if ($portableNames.Count -gt 0) { $portableNames -join ", " } else { "none" }
        throw "No DBI MTP device exposing '$RawStorageName' was found. Start DBI's MTP responder and keep the USB connection active. Portable devices visible in This PC: $available"
    }
    if ($candidates.Count -gt 1) {
        $names = ($candidates | ForEach-Object { $_.Name }) -join ", "
        throw "Multiple MTP devices expose '$RawStorageName': $names. Re-run with -DeviceName and one exact name."
    }
    return $candidates[0]
}

function Wait-ForShellChild {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Folder,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [datetime]$Deadline
    )

    do {
        $item = Get-ShellChild -Folder $Folder -Name $Name
        if ($null -ne $item) {
            return $item
        }
        Start-Sleep -Milliseconds 250
    } while ([datetime]::UtcNow -lt $Deadline)
    throw "MTP operation timed out while waiting for: $Name"
}

function Get-OrCreateMtpDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [object]$ParentFolder,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [datetime]$Deadline
    )

    $existing = Get-ShellChild -Folder $ParentFolder -Name $Name
    if ($null -eq $existing) {
        $ParentFolder.NewFolder($Name, $script:copyFlags)
        $existing = Wait-ForShellChild -Folder $ParentFolder -Name $Name -Deadline $Deadline
    }
    if (-not $existing.IsFolder) {
        throw "MTP destination path component is not a directory: $Name"
    }
    return $existing.GetFolder
}

function Resolve-MtpDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [object]$RootFolder,

        [string]$RelativePath,

        [Parameter(Mandatory = $true)]
        [datetime]$Deadline,

        [switch]$Create
    )

    $current = $RootFolder
    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $current
    }
    foreach ($segment in ($RelativePath -split '[\\/]')) {
        if ([string]::IsNullOrWhiteSpace($segment)) {
            continue
        }
        if ($Create.IsPresent) {
            $current = Get-OrCreateMtpDirectory -ParentFolder $current -Name $segment -Deadline $Deadline
        } else {
            $current = Get-ChildFolder -Folder $current -Name $segment
            if ($null -eq $current) {
                return $null
            }
        }
    }
    return $current
}

function Test-MtpFileHash {
    param(
        [Parameter(Mandatory = $true)]
        [object]$RemoteFolder,

        [Parameter(Mandatory = $true)]
        [string]$FileName,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedHash,

        [Parameter(Mandatory = $true)]
        [object]$Shell,

        [Parameter(Mandatory = $true)]
        [datetime]$Deadline
    )

    while ([datetime]::UtcNow -lt $Deadline) {
        $remoteItem = Get-ShellChild -Folder $RemoteFolder -Name $FileName
        if ($null -eq $remoteItem -or $remoteItem.IsFolder) {
            Start-Sleep -Milliseconds 250
            continue
        }

        $verifyDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("playwise-mtp-verify-" + [guid]::NewGuid().ToString("N"))
        [System.IO.Directory]::CreateDirectory($verifyDirectory) | Out-Null
        try {
            $localFolder = $Shell.NameSpace($verifyDirectory)
            if ($null -eq $localFolder) {
                throw "Unable to open the temporary verification directory."
            }
            $localFolder.CopyHere($remoteItem, $script:copyFlags)
            $localFile = Join-Path $verifyDirectory $FileName
            do {
                if (Test-Path -LiteralPath $localFile -PathType Leaf) {
                    try {
                        $actualHash = Get-Sha256 -LiteralPath $localFile
                        if ($actualHash -eq $ExpectedHash) {
                            return $true
                        }
                        break
                    } catch [System.IO.IOException] {
                        # The Shell copy is asynchronous; retry until the file closes.
                    }
                }
                Start-Sleep -Milliseconds 250
            } while ([datetime]::UtcNow -lt $Deadline)
        } finally {
            if (Test-Path -LiteralPath $verifyDirectory) {
                Remove-Item -LiteralPath $verifyDirectory -Recurse -Force
            }
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Copy-FileToMtpVerified {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceFile,

        [Parameter(Mandatory = $true)]
        [object]$DestinationFolder,

        [Parameter(Mandatory = $true)]
        [object]$Shell,

        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $sourceItem = Get-Item -LiteralPath $SourceFile -ErrorAction Stop
    $expectedHash = Get-Sha256 -LiteralPath $sourceItem.FullName
    $DestinationFolder.CopyHere($sourceItem.FullName, $script:copyFlags)
    $deadline = [datetime]::UtcNow.AddSeconds($Timeout)
    if (-not (Test-MtpFileHash -RemoteFolder $DestinationFolder -FileName $sourceItem.Name -ExpectedHash $expectedHash -Shell $Shell -Deadline $deadline)) {
        throw "Verification failed after copying through MTP: $($sourceItem.FullName)"
    }
}

if ($StorageName -match '(?i)install') {
    throw "StorageName '$StorageName' is an installer endpoint, not the raw SD root. Use DBI's 'SD Card' entry."
}

$sourceRoot = Resolve-StandardPackageRoot -LiteralPath $SourceFolder
$releaseId = Assert-StandardPackage -PackageRoot $sourceRoot
$sourceDirectories = @(
    Get-ChildItem -LiteralPath $sourceRoot -Directory -Recurse -Force |
        Sort-Object { $_.FullName.Length }
)
$sourceFiles = @(
    Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Force |
        Sort-Object FullName
)

Write-Host "Source package: $sourceRoot"
Write-Host "Release ID:     $releaseId"
Write-Host "Destination:    DBI MTP / $StorageName"
Write-Host "Install mode:   Incremental update (preserves existing config and data)"
Write-Host "Directories:    $($sourceDirectories.Count)"
Write-Host "Files:          $($sourceFiles.Count)"
Write-Host "Verification:   SHA-256 read-back through MTP"

if (-not $Apply.IsPresent) {
    Write-Host ""
    Write-Host "Preview only. No device was accessed and no files were changed."
    Write-Host "Start DBI's MTP responder, connect USB, then re-run with -Apply."
    return
}

$shell = New-Object -ComObject Shell.Application
$thisPc = $shell.NameSpace(17) # ssfDRIVES / This PC, independent of UI language
if ($null -eq $thisPc) {
    throw "Windows Shell could not open This PC."
}
$target = Resolve-DbiStorage -ThisPc $thisPc -RequestedDeviceName $DeviceName -RawStorageName $StorageName
Write-Host "MTP device:     $($target.Name)"

if (-not $WhatIfPreference) {
    $confirmation = Read-Host "Type $($target.Name) to confirm copying PlayWise to DBI MTP / $StorageName"
    if ($confirmation -cne $target.Name) {
        throw "Confirmation did not match the MTP device name; no files were changed."
    }
}

foreach ($sourceDirectory in $sourceDirectories) {
    $relativePath = $sourceDirectory.FullName.Substring($sourceRoot.Length).TrimStart('\')
    $displayPath = "$StorageName\$relativePath"
    if ($PSCmdlet.ShouldProcess($displayPath, "Create package directory through DBI MTP")) {
        $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        Resolve-MtpDirectory -RootFolder $target.StorageFolder -RelativePath $relativePath -Deadline $deadline -Create | Out-Null
    }
}

foreach ($sourceFile in $sourceFiles) {
    $relativePath = $sourceFile.FullName.Substring($sourceRoot.Length).TrimStart('\')
    $relativeParent = Split-Path -Parent $relativePath
    $displayPath = "$StorageName\$relativePath"
    if ($PSCmdlet.ShouldProcess($displayPath, "Copy and verify package file through DBI MTP")) {
        $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        $destinationFolder = Resolve-MtpDirectory -RootFolder $target.StorageFolder -RelativePath $relativeParent -Deadline $deadline -Create
        Copy-FileToMtpVerified -SourceFile $sourceFile.FullName -DestinationFolder $destinationFolder -Shell $shell -Timeout $TimeoutSeconds
        Write-Host "Verified:       $relativePath"
    }
}

if ($WhatIfPreference) {
    Write-Host "WhatIf preview completed. No files were changed."
} else {
    Write-Host "Installation completed. Every package file passed SHA-256 read-back verification."
    Write-Host "Reopen PlayWise and use the parent flow to confirm loading the new background build."
}
