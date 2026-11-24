from __future__ import annotations

from copy import deepcopy
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
import subprocess
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.automation import eden_runtime as runtime


@contextmanager
def isolated_process_snapshot():
    """Inject only the OS process query in fixture PowerShell, without a production bypass."""
    run = subprocess.run
    def execute(command, **options):
        if '-File' in command and Path(command[command.index('-File') + 1]).name == 'eden_runtime_cleanup.ps1':
            index = command.index('-File')
            script, *arguments = command[index + 1:]
            env = {**os.environ, 'PW_TEST_SCRIPT': script, 'PW_TEST_ARGS': json.dumps(arguments)}
            wrapper = ("$ErrorActionPreference='Stop'; function Get-CimInstance { @() }; "
                       "$raw=ConvertFrom-Json $env:PW_TEST_ARGS; $a=@($raw | ForEach-Object { $_ }); $p=@{}; "
                       "for($i=0;$i -lt $a.Count;$i++){ $k=$a[$i].TrimStart('-'); "
                       "if($k -eq 'ReleaseOnly'){$p[$k]=$true}else{$i++;$p[$k]=$a[$i]} }; "
                       "try { & $env:PW_TEST_SCRIPT @p; exit 0 } catch { [Console]::Error.WriteLine($_); exit 1 }")
            command = [runtime.powershell_exe(), '-NoProfile', '-Command', wrapper]
            options['env'] = env
            return run(command, **options)
        return run(command, **options)
    with patch.object(runtime, 'no_emulator'), patch.object(runtime.subprocess, 'run', side_effect=execute):
        yield


def rejected(function) -> None:
    try:
        function()
    except (OSError, ValueError):
        return
    raise AssertionError('unsafe runtime operation accepted')


def fixture(root: Path) -> tuple:
    eden = root / 'source/eden.exe'
    source = root / 'source/user'
    for name, data in [('config/qt-config.ini', 'private-config'), ('keys/test.keys', 'private-key'),
                       ('nand/system/Contents/registered/firmware.nca', 'fixed-firmware'),
                       ('nand/system/save/mutable.bin', 'save'), ('nand/user/Contents/unrelated-game.nca', 'omit-game')]:
        p = source / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(data, encoding='utf-8')
    eden.write_bytes(b'host-fixture-not-an-emulator')
    lang = eden.parent / 'languages/zh.qm'
    lang.parent.mkdir()
    lang.write_bytes(b'read-only-language')
    cache = root / 'cache/Contents'
    (cache / 'registered').mkdir(parents=True)
    (cache / 'registered/firmware.nca').write_text('fixed-firmware', encoding='utf-8')
    return eden, source, root / 'profiles', root / 'aliases', cache


def test_source_selection_and_capacity() -> None:
    with tempfile.TemporaryDirectory() as folder:
        eden, source, data, aliases, cache = fixture(Path(folder))
        rels = {rel.as_posix() for p, rel in runtime.mutable_sources(source)}
        assert rels == {'config/qt-config.ini', 'keys/test.keys', 'nand/system/save/mutable.bin'}
        with patch.object(runtime.shutil, 'disk_usage', return_value=type('Usage', (), {'free': runtime.RESERVE + 9})()):
            assert runtime.capacity(data, 9)['copy_bytes'] == 9
            rejected(lambda: runtime.capacity(data, 10))
        assert not list(data.iterdir())


def test_firmware_access_without_content_writes() -> None:
    with tempfile.TemporaryDirectory() as folder:
        eden, source, data, aliases, cache = fixture(Path(folder))
        expected = runtime.tree(cache)
        item = cache / 'registered/firmware.nca'
        if os.name == 'nt':
            item.chmod(stat.S_IREAD)
            rejected(lambda: item.open('r+b'))
        report = runtime.firmware_access(cache, expected)
        assert report['files_checked'] == 1 and report['content_unchanged'] is True
        assert report['content_fingerprint_before'] == report['content_fingerprint_after']
        assert report['read_only_cleared'] == (['registered/firmware.nca'] if os.name == 'nt' else [])
        with item.open('r+b'):
            pass
        assert runtime.tree(cache) == expected
        item.write_text('tampered', encoding='utf-8')
        rejected(lambda: runtime.firmware_access(cache, expected))


def has_powershell() -> bool:
    return os.name == 'nt' and bool(shutil.which('pwsh') or shutil.which('powershell'))


def test_cache_tamper_and_lease_conflict() -> None:
    if not has_powershell():
        return
    with tempfile.TemporaryDirectory() as folder, isolated_process_snapshot():
        eden, source, data, aliases, cache = fixture(Path(folder))
        out = Path(folder) / 'report'
        out.mkdir()
        rejected(lambda: runtime.prepare(eden, source, data, Path('Z:/PlayWise-test-aliases'), cache, out))
        assert not (out / 'runtime-ownership.json').exists()
        (cache / 'registered/firmware.nca').write_text('corrupt', encoding='utf-8')
        rejected(lambda: runtime.prepare(eden, source, data, aliases, cache, out))
        assert not (out / 'runtime-ownership.json').exists() and not list(data.iterdir())
        (cache / 'registered/firmware.nca').write_text('fixed-firmware', encoding='utf-8')
        runtime.write(aliases / '.playwise-eden-lease.json', {'run_id': 'another-owner'})
        rejected(lambda: runtime.prepare(eden, source, data, aliases, cache, out))
        assert json.loads((aliases / '.playwise-eden-lease.json').read_text()) == {'run_id': 'another-owner'}
        assert not (out / 'runtime-ownership.json').exists()


def test_hardlink_isolation_cleanup_and_protection() -> None:
    if not has_powershell():
        return
    with tempfile.TemporaryDirectory() as folder, isolated_process_snapshot():
        eden, source, data, aliases, cache = fixture(Path(folder))
        out = Path(folder) / 'report'
        out.mkdir()
        entry, profile, owner = runtime.prepare(eden, source, data, aliases, cache, out)
        assert os.path.samefile(entry / 'eden.exe', eden) and (entry / 'user').resolve() == profile.resolve()
        assert (profile / 'nand/system/Contents').resolve() == cache.resolve()
        assert not (profile / 'nand/user/Contents/unrelated-game.nca').exists()
        assert owner['application_copied_bytes'] == 0 and owner['firmware_cache_copied_bytes'] == 0
        (profile / 'config/qt-config.ini').write_text('changed-only-private', encoding='utf-8')
        runtime.verify_protected(owner)
        assert (source / 'config/qt-config.ini').read_text() == 'private-config'
        tampered = deepcopy(owner)
        tampered['links'][0]['target'] = str(source)
        rejected(lambda: runtime.validate_ownership(tampered))
        tampered = deepcopy(owner)
        tampered['storage'] = str(source)
        rejected(lambda: runtime.validate_ownership(tampered))
        # Cleanup must reject an altered junction and leave all source bytes intact.
        script = Path(__file__).parents[2] / 'tools/automation/eden_runtime_cleanup.ps1'
        wrong = deepcopy(owner)
        wrong['links'][0]['target'] = str(source)
        path = out / 'wrong-ownership.json'
        runtime.write(path, wrong)
        completed = subprocess.run(['pwsh', '-NoProfile', '-File', str(script), '-Ownership', str(path),
                                    '-OwnershipSha256', runtime.digest(path), '-Result', str(out / 'bad-result.json')], capture_output=True)
        assert completed.returncode != 0 and profile.exists() and (aliases / '.playwise-eden-lease.json').exists()
        (out / 'evidence-manifest.json').write_text('{}', encoding='utf-8')
        result = runtime.finish(out, True)
        assert result['status'] == 'removed' and not entry.exists() and not profile.parent.exists()
        assert not (aliases / '.playwise-eden-lease.json').exists()
        runtime.verify_protected(owner)
        assert (cache / 'registered/firmware.nca').read_text() == 'fixed-firmware'
        assert runtime.verify_cleanup(out) == runtime.verify_cleanup(out) == result
        check = out.with_name('report-cleanup') / 'verification.json'
        changed = json.loads(check.read_text(encoding='utf-8'))
        changed['schema_version'] = True
        check.write_text(json.dumps(changed), encoding='utf-8')
        rejected(lambda: runtime.verify_cleanup(out))


def test_prepare_failure_retains_without_blocking_next_batch() -> None:
    if not has_powershell():
        return
    with tempfile.TemporaryDirectory() as folder, isolated_process_snapshot():
        eden, source, data, aliases, cache = fixture(Path(folder))
        out = Path(folder) / 'report'
        out.mkdir()
        with patch.object(runtime, 'link', side_effect=ValueError('injected link creation failure')):
            rejected(lambda: runtime.prepare(eden, source, data, aliases, cache, out))
        owner = json.loads((out / 'runtime-ownership.json').read_text())
        assert Path(owner['storage']).exists()
        result = runtime.finish(out, False)
        assert result['status'] == 'retained_for_review' and Path(owner['storage']).exists()
        assert not (aliases / '.playwise-eden-lease.json').exists()
        retained = json.loads((out.with_name('report-cleanup') / 'retention.json').read_text())
        assert retained['review_by_utc'] and retained['reason']


def test_prune_expired_dry_run_age_and_gates() -> None:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        data = root / 'profiles'
        aliases = root / 'aliases'
        cache = root / 'cache/Contents'
        (cache / 'registered').mkdir(parents=True)
        (cache / 'registered/fw.nca').write_text('fw', encoding='utf-8')
        data.mkdir()
        aliases.mkdir()

        old_id = 'a' * 32
        fresh_id = 'b' * 32
        other = data / 'not-a-uuid'
        other.mkdir()
        (other / 'keep.txt').write_text('keep', encoding='utf-8')
        for rid in (old_id, fresh_id):
            (data / rid / 'user').mkdir(parents=True)
            (data / rid / 'user/state.bin').write_bytes(b'x')
            (aliases / rid).mkdir(parents=True)
            (aliases / rid / 'dummy.txt').write_text('d', encoding='utf-8')

        now = datetime(2026, 11, 24, 12, 0, 0, tzinfo=timezone.utc)
        old_ts = (now - timedelta(hours=30)).timestamp()
        fresh_ts = (now - timedelta(hours=4)).timestamp()
        for p in (data / old_id, data / old_id / 'user', aliases / old_id):
            os.utime(p, (old_ts, old_ts))
        for p in (data / fresh_id, data / fresh_id / 'user', aliases / fresh_id):
            os.utime(p, (fresh_ts, fresh_ts))

        # Dry-run lists expired run without mutating anything.
        dry = runtime.prune_expired(data, aliases, cache, min_age_hours=24.0, apply=False, now_utc=now)
        assert dry['mode'] == 'dry_run'
        assert dry['eligible'] == [old_id] and dry['removed'] == []
        assert [r['run_id'] for r in dry['skipped_recent']] == [fresh_id]
        assert str(other) in dry['skipped_unexpected']
        assert dry['rejected'] == []
        assert (data / old_id).exists() and (aliases / old_id).exists()

        # Active lease blocks both dry-run and apply.
        lease = aliases / '.playwise-eden-lease.json'
        runtime.write(lease, {'run_id': fresh_id})
        rejected(lambda: runtime.prune_expired(data, aliases, cache, apply=False, now_utc=now))
        rejected(lambda: runtime.prune_expired(data, aliases, cache, apply=True, now_utc=now))
        lease.unlink()

        # Overlapping protected/managed roots are rejected.
        rejected(lambda: runtime.prune_expired(data, data / 'sub', cache, apply=False, now_utc=now))
        rejected(lambda: runtime.prune_expired(data, aliases, data / 'fw', apply=False, now_utc=now))
        rejected(lambda: runtime.prune_expired(data, aliases, cache, min_age_hours=-1.0, now_utc=now))

        # Apply removes only the expired UUID run, preserving recent and non-UUID entries.
        with patch.object(runtime, 'no_emulator'):
            applied = runtime.prune_expired(data, aliases, cache, min_age_hours=24.0, apply=True, now_utc=now)
        assert applied['mode'] == 'apply' and applied['removed'] == [old_id]
        assert not (data / old_id).exists() and not (aliases / old_id).exists()
        assert (data / fresh_id).exists() and (aliases / fresh_id).exists()
        assert (other / 'keep.txt').read_text(encoding='utf-8') == 'keep'
        assert (cache / 'registered/fw.nca').read_text(encoding='utf-8') == 'fw'


def test_prune_expired_windows_junctions_and_rejection() -> None:
    if not has_powershell():
        return
    with tempfile.TemporaryDirectory() as folder, isolated_process_snapshot():
        eden, source, data, aliases, cache = fixture(Path(folder))
        out1 = Path(folder) / 'report-expired'
        out1.mkdir()
        entry1, profile1, owner1 = runtime.prepare(eden, source, data, aliases, cache, out1)
        assert runtime.finish(out1, False)['status'] == 'retained_for_review'
        run1 = owner1['run_id']

        # Retained run with tampered junction target must be rejected without touching targets.
        out2 = Path(folder) / 'report-tampered'
        out2.mkdir()
        entry2, profile2, owner2 = runtime.prepare(eden, source, data, aliases, cache, out2)
        assert runtime.finish(out2, False)['status'] == 'retained_for_review'
        run2 = owner2['run_id']
        os.rmdir(entry2 / 'user')
        runtime.link(entry2 / 'user', source, 'Junction')

        future_12h = datetime.now(timezone.utc) + timedelta(hours=12)
        early = runtime.prune_expired(
            data, aliases, cache, eden.parent / 'languages',
            min_age_hours=24.0, apply=True, now_utc=future_12h,
        )
        assert early['removed'] == [] and {r['run_id'] for r in early['skipped_recent']} == {run1, run2}

        future_30h = datetime.now(timezone.utc) + timedelta(hours=30)
        res = runtime.prune_expired(
            data, aliases, cache, eden.parent / 'languages',
            min_age_hours=24.0, apply=True, now_utc=future_30h,
        )
        assert res['removed'] == [run1]
        assert [r['run_id'] for r in res['rejected']] == [run2]
        assert not entry1.exists() and not profile1.parent.exists()
        # Tampered run was not deleted, and all protected sources/cache stayed untouched.
        assert entry2.exists() and profile2.exists()
        assert (source / 'config/qt-config.ini').read_text(encoding='utf-8') == 'private-config'
        assert (cache / 'registered/firmware.nca').read_text(encoding='utf-8') == 'fixed-firmware'
        assert (eden.parent / 'languages/zh.qm').read_bytes() == b'read-only-language'

        # Clean up run2 safely after fixing its junction so TemporaryDirectory exits cleanly.
        os.rmdir(entry2 / 'user')
        runtime.link(entry2 / 'user', profile2, 'Junction')
        res2 = runtime.prune_expired(
            data, aliases, cache, eden.parent / 'languages',
            min_age_hours=24.0, apply=True, now_utc=future_30h,
        )
        assert res2['removed'] == [run2] and res2['rejected'] == []
        assert os.lstat(eden).st_nlink == 1


def main() -> None:
    test_source_selection_and_capacity()
    test_firmware_access_without_content_writes()
    test_cache_tamper_and_lease_conflict()
    test_hardlink_isolation_cleanup_and_protection()
    test_prepare_failure_retains_without_blocking_next_batch()
    test_prune_expired_dry_run_age_and_gates()
    test_prune_expired_windows_junctions_and_rejection()
    if has_powershell():
        print('PASS: shared Eden resources, isolation, cache/lease/capacity gates and owned cleanup')
    else:
        print('PASS: source selection/capacity; SKIP: native NTFS isolation/cleanup requires Windows and pwsh/powershell')


if __name__ == '__main__':
    main()
