param([Parameter(Mandatory)][string]$Ownership,[Parameter(Mandatory)][string]$OwnershipSha256,
      [Parameter(Mandatory)][string]$Result,[switch]$ReleaseOnly)
$ErrorActionPreference='Stop'
if ((Get-FileHash -LiteralPath $Ownership -Algorithm SHA256).Hash -ne $OwnershipSha256) { throw 'Ownership changed' }
$owner=Get-Content -LiteralPath $Ownership -Raw -Encoding utf8 | ConvertFrom-Json
if ($owner.schema_version -ne 1 -or $owner.mode -ne 'shared' -or $owner.run_id -notmatch '^[0-9a-f]{32}$') { throw 'Invalid ownership' }
function Full($path) { [IO.Path]::GetFullPath($path).TrimEnd('\') }
function Ordinary($path) {
    $current=$path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse root: $current" }
        }
        $parent=Split-Path $current
        if ($parent -eq $current) { break }
        $current=$parent
    }
}
function NoEmulator {
    if (@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '(?i)eden|yuzu|suyu|sudachi' }).Count) { throw 'Emulator active; no cleanup' }
}
$runtime=Full $owner.runtime
$storage=Full $owner.storage
$profile=Full $owner.profile
if ($runtime -ne (Full (Join-Path $owner.application_root $owner.run_id)) -or
    $storage -ne (Full (Join-Path $owner.runtime_root $owner.run_id)) -or
    $profile -ne (Full (Join-Path $storage 'user'))) { throw 'Owned delete paths differ' }
Ordinary $owner.application_root
Ordinary $owner.runtime_root
Ordinary $runtime
Ordinary $storage
if ((Full $owner.lease) -ne (Full (Join-Path $owner.application_root '.playwise-eden-lease.json'))) { throw 'Lease escapes root' }
$lease=Get-Content -LiteralPath $owner.lease -Raw -Encoding utf8 | ConvertFrom-Json
if ($lease.run_id -ne $owner.run_id -or $lease.owner_pid -ne $owner.lease_record.owner_pid -or $lease.output -ne $owner.lease_record.output) { throw 'Lease belongs to another run' }
NoEmulator
$expected=@{}
$expected[(Full (Join-Path $runtime 'user'))]=(Full $profile)
$expected[(Full (Join-Path $profile 'nand\system\Contents'))]=(Full $owner.firmware_cache)
if ($owner.protected_before.languages.Count) { $expected[(Full (Join-Path $runtime 'languages'))]=(Full (Join-Path (Split-Path $owner.source_executable) 'languages')) }
if ($owner.links.Count -ne $expected.Count) { throw 'Link count differs' }
foreach ($entry in $owner.links) {
    $path=Full $entry.path
    if (-not $expected.ContainsKey($path) -or (Full $entry.target) -ne $expected[$path]) { throw 'Unexpected link in manifest' }
    if (-not $ReleaseOnly) {
        $item=Get-Item -LiteralPath $path -Force
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or (Full $item.ResolveLinkTarget($true).FullName) -ne $expected[$path]) { throw 'Junction target differs' }
    }
}
if (Test-Path -LiteralPath $Result) { throw 'Cleanup result already exists; no replay' }
$journal="$Result.events.jsonl"
if (Test-Path -LiteralPath $journal) { throw 'Cleanup journal already exists; inspect partial cleanup' }
function Event($action,$path) { @{action=$action;path=$path;at_utc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json -Compress | Add-Content -LiteralPath $journal -Encoding utf8NoBOM }
if (-not $ReleaseOnly) {
    # Validate every link before any mutation, then unlink only the entries, never their targets.
    foreach ($entry in $owner.links) {
        Event 'unlink_started' $entry.path
        [IO.Directory]::Delete($entry.path,$false)
        Event 'unlinked' $entry.path
    }
    foreach ($path in @($runtime,$storage)) {
        Ordinary $path
        if (@(Get-ChildItem -LiteralPath $path -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Unexpected reparse child; abort' }
        NoEmulator
        Event 'remove_started' $path
        Remove-Item -LiteralPath $path -Recurse
        Event 'removed' $path
    }
}
Event 'release_lease_started' $owner.lease
Remove-Item -LiteralPath $owner.lease
Event 'lease_released' $owner.lease
@{schema_version=1;status=$(if ($ReleaseOnly) {'retained_for_review'} else {'removed'});run_id=$owner.run_id;runtime=$runtime;storage=$storage;source_executable=$owner.source_executable;firmware_cache_retained=$owner.firmware_cache} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Result -Encoding utf8NoBOM
