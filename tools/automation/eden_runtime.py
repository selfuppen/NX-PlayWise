"""Owned, lightweight Windows Eden profiles; no application bytes copied per run."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time
import uuid

RESERVE = 512 * 1024 * 1024


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def digest(path: Path) -> str:
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as output:
        json.dump(value, output, ensure_ascii=False, indent=2)


def ordinary(path: Path) -> None:
    """Never create or recursively remove owned roots through a reparse point."""
    for parent in (path, *path.parents):
        if parent.exists():
            require(not parent.is_symlink(), f'reparse root prohibited: {parent}')
            require(not (getattr(parent.lstat(), 'st_file_attributes', 0) & 1024),
                    f'reparse root prohibited: {parent}')


def files(root: Path) -> list[Path]:
    ordinary(root)
    result = []
    for base, dirs, names in os.walk(root, followlinks=False):
        for name in dirs + names:
            p = Path(base) / name
            require(not (getattr(p.lstat(), 'st_file_attributes', 0) & 1024) and not p.is_symlink(),
                    f'reparse source prohibited: {p}')
        result.extend(Path(base) / name for name in names)
    return sorted(result)


def tree(root: Path) -> list[dict]:
    return [{'path': p.relative_to(root).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p)}
            for p in files(root)]


def fingerprint(value: object) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def mutable_sources(profile: Path) -> list[tuple[Path, Path]]:
    result = []
    for name in ('config', 'keys', 'nand'):
        require((profile / name).is_dir(), f'profile component missing: {name}')
        for p in files(profile / name):
            rel = p.relative_to(profile)
            # Installed content is immutable resource data, never mutable save state.
            if name == 'nand' and len(rel.parts) >= 3 and rel.parts[2] == 'Contents':
                continue
            result.append((p, rel))
    return result


def capacity(root: Path, needed: int) -> dict:
    ordinary(root)
    root.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(root).free
    require(free >= needed + RESERVE, f'insufficient shared runtime space: {root}; no runtime prepared')
    return {'copy_bytes': needed, 'reserve_bytes': RESERVE, 'free_before': free}


def powershell_exe() -> str:
    return shutil.which('pwsh') or shutil.which('powershell') or 'powershell'


def powershell(script: str, **env: str) -> str:
    result = subprocess.run([powershell_exe(), '-NoProfile', '-NonInteractive', '-Command', script],
                            env={**os.environ, **env}, capture_output=True,
                            encoding='utf-8', errors='strict')
    require(result.returncode == 0, f'PowerShell gate failed: {result.stderr.strip()}')
    return result.stdout


def no_emulator() -> None:
    powershell("$ErrorActionPreference='Stop'; if (@(Get-CimInstance Win32_Process | Where-Object { "
               "$_.Name -match '(?i)eden|yuzu|suyu|sudachi' }).Count) { throw 'Existing emulator; no launch or cleanup' }")


def link(path: Path, target: Path, kind: str) -> None:
    require(not path.exists(), f'owned link already exists: {path}')
    path.parent.mkdir(parents=True, exist_ok=True)
    powershell("$ErrorActionPreference='Stop'; New-Item -ItemType $env:PW_LINK_KIND "
               "-Path $env:PW_LINK_PATH -Target $env:PW_LINK_TARGET | Out-Null",
               PW_LINK_KIND=kind, PW_LINK_PATH=str(path), PW_LINK_TARGET=str(target))


def protected(eden: Path, profile: Path) -> dict:
    languages = eden.parent / 'languages'
    return {'exe_sha256': digest(eden), 'profile': {name: tree(profile / name) for name in ('config', 'keys', 'nand')},
            'languages': tree(languages) if languages.is_dir() else []}


def firmware_access(cache: Path, expected: list[dict]) -> dict:
    """Eden opens NCA files read/write even when only reading their content.

    Only the owned cache's read-only attribute may change, under the run lease.
    No bytes are written; full hashes remain the immutable-resource contract.
    """
    require(tree(cache) == expected, 'firmware cache differs before access check')
    cleared = []
    for path in files(cache):
        info = path.stat()
        if getattr(info, 'st_file_attributes', 0) & 1:
            path.chmod(info.st_mode | stat.S_IWRITE)
            cleared.append(path.relative_to(cache).as_posix())
        with path.open('r+b'):
            pass  # Check the actual handle mode without modifying firmware data.
    require(tree(cache) == expected, 'firmware bytes changed during access check')
    return {'schema_version': 1, 'policy': 'writable_handles_content_hash_guard_v1',
            'cache': str(cache), 'files_checked': len(expected), 'read_only_cleared': cleared,
            'content_fingerprint_before': fingerprint(expected),
            'content_fingerprint_after': fingerprint(tree(cache)), 'content_unchanged': True}


def prepare(eden: Path, source: Path, runtime_root: Path, application_root: Path,
            firmware_cache: Path, output: Path) -> tuple[Path, Path, dict]:
    require(os.name == 'nt', 'shared mode requires Windows NTFS hardlinks/junctions')
    started = time.monotonic()
    eden, source = Path(os.path.abspath(eden)), Path(os.path.abspath(source))
    runtime_root, application_root, firmware_cache = (Path(os.path.abspath(p)) for p in (runtime_root, application_root, firmware_cache))
    for p in (eden, source, runtime_root, application_root, firmware_cache):
        ordinary(p)
    require(eden.drive.lower() == application_root.drive.lower(), 'application hardlinks require the same volume; no copy fallback')
    require(not any(p == source or p.is_relative_to(source) or source.is_relative_to(p)
                    for p in (runtime_root, application_root, firmware_cache)), 'managed roots overlap source profile')
    require(not any(a == b or a.is_relative_to(b) or b.is_relative_to(a)
                    for a, b in ((runtime_root, application_root), (runtime_root, firmware_cache), (application_root, firmware_cache))),
            'managed roots overlap each other')
    no_emulator()
    selected = mutable_sources(source)
    copied = sum(p.stat().st_size for p, _ in selected)
    budget = capacity(runtime_root, copied)
    capacity(application_root, 0)
    firmware = tree(source / 'nand/system/Contents')
    require(bool(firmware), 'firmware resource missing')
    if firmware_cache.exists():
        require(tree(firmware_cache) == firmware, 'shared firmware cache differs; never overwrite it')
        cache_copied = 0
    else:
        capacity(firmware_cache.parent, sum(row['bytes'] for row in firmware))
        shutil.copytree(source / 'nand/system/Contents', firmware_cache)
        require(tree(firmware_cache) == firmware, 'firmware copy verification differs')
        cache_copied = sum(row['bytes'] for row in firmware)
    run_id = uuid.uuid4().hex
    runtime = application_root / run_id
    storage = runtime_root / run_id
    profile = storage / 'user'
    # The exclusive lease spans preparation, all native launches, sealing and cleanup.
    lease = application_root / '.playwise-eden-lease.json'
    lease_record = {'schema_version': 1, 'run_id': run_id, 'owner_pid': os.getpid(), 'output': str(output)}
    protected_before = protected(eden, source)
    write(lease, lease_record)
    links = [{'path': str(runtime / 'user'), 'target': str(profile)},
             {'path': str(profile / 'nand/system/Contents'), 'target': str(firmware_cache)}]
    if (eden.parent / 'languages').is_dir():
        links.append({'path': str(runtime / 'languages'), 'target': str(eden.parent / 'languages')})
    ownership = {'schema_version': 1, 'mode': 'shared', 'run_id': run_id,
                 'runtime': str(runtime), 'storage': str(storage), 'profile': str(profile),
                 'runtime_root': str(runtime_root), 'application_root': str(application_root),
                 'source_executable': str(eden), 'source_profile': str(source),
                 'firmware_cache': str(firmware_cache), 'links': links, 'lease': str(lease),
                 'lease_record': lease_record, 'protected_before': protected_before,
                 'firmware_before': firmware, 'application_copied_bytes': 0,
                 'profile_copied_bytes': copied, 'firmware_cache_copied_bytes': cache_copied,
                 'runtime_capacity': budget}
    write(output / 'runtime-ownership.json', ownership)
    write(output / 'firmware-access.json', firmware_access(firmware_cache, firmware))
    profile.mkdir(parents=True, exist_ok=False)
    runtime.mkdir(exist_ok=False)
    for p, rel in selected:
        target = profile / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
    link(runtime / 'eden.exe', eden, 'HardLink')
    require(os.path.samefile(runtime / 'eden.exe', eden), 'program is not a hardlink')
    for row in links:
        link(Path(row['path']), Path(row['target']), 'Junction')
    ownership['preparation_seconds'] = time.monotonic() - started
    write(output / 'runtime-prepared.json', {k: v for k, v in ownership.items() if k != 'protected_before'})
    return runtime, profile, ownership


def verify_protected(ownership: dict) -> None:
    require(protected(Path(ownership['source_executable']), Path(ownership['source_profile'])) == ownership['protected_before'],
            'source installation/profile changed during run')
    require(tree(Path(ownership['firmware_cache'])) == ownership['firmware_before'], 'shared firmware changed during run')


def validate_ownership(value: dict) -> None:
    run_id = value['run_id']
    require(type(value['schema_version']) is int and value['schema_version'] == 1 and value['mode'] == 'shared' and
            re.fullmatch('[0-9a-f]{32}', run_id) is not None, 'invalid runtime ownership')
    require(Path(value['runtime']) == Path(value['application_root']) / run_id and
            Path(value['storage']) == Path(value['runtime_root']) / run_id and
            Path(value['profile']) == Path(value['storage']) / 'user', 'owned roots differ')
    expected = {(str(Path(value['runtime']) / 'user'), value['profile']),
                (str(Path(value['profile']) / 'nand/system/Contents'), value['firmware_cache'])}
    if value['protected_before']['languages']:
        expected.add((str(Path(value['runtime']) / 'languages'), str(Path(value['source_executable']).parent / 'languages')))
    require({(r['path'], r['target']) for r in value['links']} == expected and len(value['links']) == len(expected),
            'owned junctions differ')
    require(Path(value['lease']) == Path(value['application_root']) / '.playwise-eden-lease.json' and
            value['lease_record']['run_id'] == run_id, 'lease ownership differs')


def finish(output: Path, success: bool) -> dict:
    """Append a separate cleanup result; sealed raw evidence is never rewritten."""
    source = output / 'runtime-ownership.json'
    value = json.loads(source.read_text(encoding='utf-8'))
    validate_ownership(value)
    supplement = output.with_name(output.name + '-cleanup')
    supplement.mkdir(exist_ok=False)
    if success:
        verify_protected(value)
    else:
        write(supplement / 'retention.json', {'schema_version': 1, 'status': 'retained_for_review',
              'reason': 'failed/unknown run or preparation; no automatic deletion',
              'runtime': value['runtime'], 'storage': value['storage'],
              'review_by_utc': (datetime.now(timezone.utc) + timedelta(hours=24)).isoformat()})
    command = [powershell_exe(), '-NoProfile', '-NonInteractive', '-File',
               str(Path(__file__).with_name('eden_runtime_cleanup.ps1')), '-Ownership', str(source),
               '-OwnershipSha256', digest(source), '-Result', str(supplement / 'result.json')]
    if not success:
        command.append('-ReleaseOnly')
    completed = subprocess.run(command, capture_output=True, encoding='utf-8', check=True)
    result = json.loads((supplement / 'result.json').read_text(encoding='utf-8-sig'))
    require(result['status'] == ('removed' if success else 'retained_for_review'), 'cleanup status differs')
    if success:
        require(not Path(value['runtime']).exists() and not Path(value['storage']).exists(), 'owned runtime remains')
        verify_protected(value)
    result['evidence_manifest_sha256'] = digest(output / 'evidence-manifest.json') if (output / 'evidence-manifest.json').exists() else None
    result['ownership_sha256'] = digest(source)
    write(supplement / 'verification.json', result)
    seal_cleanup(output)
    require(verify_cleanup(output) == verify_cleanup(output) == result, 'cleanup JSON roundtrip/repeat differs')
    return result


def seal_cleanup(output: Path) -> None:
    supplement = output.with_name(output.name + '-cleanup')
    rows = tree(supplement)
    require(not (supplement / 'evidence-manifest.json').exists(), 'cleanup already sealed')
    write(supplement / 'evidence-manifest.json', {'schema_version': 1, 'files': rows})


def verify_cleanup(output: Path) -> dict:
    supplement = output.with_name(output.name + '-cleanup')
    manifest = json.loads((supplement / 'evidence-manifest.json').read_text(encoding='utf-8'))
    require(type(manifest['schema_version']) is int and manifest['schema_version'] == 1 and type(manifest['files']) is list,
            'cleanup evidence schema differs')
    seen = set()
    for row in manifest['files']:
        path = (supplement / row['path']).resolve()
        require(path.is_relative_to(supplement.resolve()) and row['path'] not in seen and type(row['bytes']) is int and
                path.stat().st_size == row['bytes'] and digest(path) == row['sha256'], 'cleanup evidence changed')
        seen.add(row['path'])
    value = json.loads((supplement / 'verification.json').read_text(encoding='utf-8'))
    fields = {'schema_version', 'status', 'run_id', 'runtime', 'storage', 'source_executable', 'firmware_cache_retained'}
    original = json.loads((supplement / 'result.json').read_text(encoding='utf-8-sig'))
    require(set(original) == fields and set(value) == fields | {'evidence_manifest_sha256', 'ownership_sha256'} and
            type(value['schema_version']) is int and value['schema_version'] == 1 and
            value['status'] in ('removed', 'retained_for_review') and
            all(type(value[k]) is str for k in fields - {'schema_version'}) and
            all(value[k] == original[k] for k in fields), 'cleanup result schema/roundtrip differs')
    require(value['ownership_sha256'] == digest(output / 'runtime-ownership.json'), 'cleanup owner binding changed')
    expected_manifest = digest(output / 'evidence-manifest.json') if (output / 'evidence-manifest.json').exists() else None
    require(value['evidence_manifest_sha256'] == expected_manifest, 'cleanup report binding changed')
    return value


def _is_reparse(path: Path) -> bool:
    try:
        st = path.lstat()
    except OSError:
        return False
    return bool(getattr(st, 'st_file_attributes', 0) & 1024) or stat.S_ISLNK(st.st_mode)


def _norm_path(path: Path | str) -> str:
    text = str(path)
    if text.startswith('\\\\?\\'):
        text = text[4:]
    return os.path.normcase(os.path.abspath(text)).rstrip('\\/')


def _rmtree(path: Path) -> None:
    def _clear_ro(func, target: str, _exc) -> None:
        os.chmod(target, stat.S_IWRITE)
        func(target)

    shutil.rmtree(path, onerror=_clear_ro)


def _collect_reparse_points(root: Path) -> list[Path]:
    found: list[Path] = []
    if not root.exists():
        return found
    for base, dirs, names in os.walk(root, followlinks=False):
        for name in list(dirs):
            child = Path(base) / name
            if _is_reparse(child):
                found.append(child)
                dirs.remove(name)
        for name in names:
            child = Path(base) / name
            if _is_reparse(child):
                found.append(child)
    return sorted(found)


def _unlink_reparse(path: Path) -> None:
    require(_is_reparse(path), f'refusing to unlink non-reparse path: {path}')
    if os.name == 'nt' and path.is_dir():
        os.rmdir(path)
    else:
        os.unlink(path)


def prune_expired(
    runtime_root: Path,
    application_root: Path,
    firmware_cache: Path,
    source_languages: Path | None = None,
    *,
    min_age_hours: float = 24.0,
    apply: bool = False,
    now_utc: datetime | None = None,
) -> dict:
    """Safely prune expired retained Eden shared runtimes after the review window.

    Never traverses junctions/symlinks, rejects active leases or unexpected reparse
    targets, and defaults to dry-run unless apply=True.
    """
    require(min_age_hours >= 0, 'min_age_hours must be non-negative')
    runtime_root = Path(os.path.abspath(runtime_root))
    application_root = Path(os.path.abspath(application_root))
    firmware_cache = Path(os.path.abspath(firmware_cache))
    source_languages = Path(os.path.abspath(source_languages)) if source_languages is not None else None

    roots = [runtime_root, application_root, firmware_cache]
    if source_languages is not None:
        roots.append(source_languages)
    for p in roots:
        ordinary(p)
    for i, a in enumerate(roots):
        for b in roots[i + 1:]:
            require(not (a == b or a.is_relative_to(b) or b.is_relative_to(a)),
                    f'managed/protected roots overlap: {a} vs {b}')

    lease = application_root / '.playwise-eden-lease.json'
    require(not lease.exists(), f'active Eden lease exists; no gc: {lease}')
    if apply and os.name == 'nt':
        no_emulator()

    now_ts = (now_utc or datetime.now(timezone.utc)).timestamp()
    run_ids: set[str] = set()
    skipped_unexpected: list[str] = []

    for root in (runtime_root, application_root):
        if not root.exists():
            continue
        require(root.is_dir(), f'managed root is not a directory: {root}')
        for entry in sorted(os.scandir(root), key=lambda e: e.name):
            if entry.is_dir(follow_symlinks=False) and re.fullmatch('[0-9a-f]{32}', entry.name):
                run_ids.add(entry.name)
            else:
                skipped_unexpected.append(str(Path(entry.path)))

    eligible: list[str] = []
    removed: list[str] = []
    skipped_recent: list[dict] = []
    rejected: list[dict] = []

    for run_id in sorted(run_ids):
        storage = runtime_root / run_id
        runtime_dir = application_root / run_id
        mtimes: list[float] = []
        for probe in (storage, storage / 'user', runtime_dir):
            if probe.exists() and not _is_reparse(probe):
                mtimes.append(probe.lstat().st_mtime)
        newest = max(mtimes) if mtimes else now_ts
        age_hours = (now_ts - newest) / 3600.0
        if age_hours < min_age_hours:
            skipped_recent.append({'run_id': run_id, 'age_hours': round(age_hours, 2)})
            continue

        expected_links: dict[str, str] = {
            _norm_path(runtime_dir / 'user'): _norm_path(storage / 'user'),
            _norm_path(storage / 'user' / 'nand' / 'system' / 'Contents'): _norm_path(firmware_cache),
        }
        if source_languages is not None:
            expected_links[_norm_path(runtime_dir / 'languages')] = _norm_path(source_languages)

        found_links = _collect_reparse_points(runtime_dir) + _collect_reparse_points(storage)
        bad_reason: str | None = None
        for link_path in found_links:
            key = _norm_path(link_path)
            if key not in expected_links:
                bad_reason = f'unexpected reparse point: {link_path}'
                break
            try:
                actual_target = _norm_path(os.readlink(link_path))
            except OSError as exc:
                bad_reason = f'unreadable reparse target {link_path}: {exc}'
                break
            if actual_target != expected_links[key]:
                bad_reason = f'reparse target mismatch for {link_path}: {actual_target} != {expected_links[key]}'
                break

        if bad_reason is not None:
            rejected.append({'run_id': run_id, 'reason': bad_reason})
            continue

        eligible.append(run_id)
        if not apply:
            continue

        require(not lease.exists(), f'active Eden lease appeared during gc: {lease}')
        if os.name == 'nt':
            no_emulator()
        for link_path in found_links:
            _unlink_reparse(link_path)
        for target_dir in (runtime_dir, storage):
            if not target_dir.exists():
                continue
            ordinary(target_dir)
            require(not _collect_reparse_points(target_dir), f'reparse child remained before delete: {target_dir}')
            _rmtree(target_dir)
        removed.append(run_id)

    return {
        'schema_version': 1,
        'mode': 'apply' if apply else 'dry_run',
        'min_age_hours': float(min_age_hours),
        'runtime_root': str(runtime_root),
        'application_root': str(application_root),
        'firmware_cache': str(firmware_cache),
        'eligible': eligible,
        'removed': removed,
        'skipped_recent': skipped_recent,
        'skipped_unexpected': skipped_unexpected,
        'rejected': rejected,
    }
