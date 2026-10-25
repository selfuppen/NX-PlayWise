from __future__ import annotations

from copy import deepcopy
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
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
                       "$a=@(ConvertFrom-Json $env:PW_TEST_ARGS); $p=@{}; "
                       "for($i=0;$i -lt $a.Count;$i++){ $k=$a[$i].TrimStart('-'); "
                       "if($k -eq 'ReleaseOnly'){$p[$k]=$true}else{$i++;$p[$k]=$a[$i]} }; "
                       "try { & $env:PW_TEST_SCRIPT @p; exit 0 } catch { [Console]::Error.WriteLine($_); exit 1 }")
            command = ['pwsh', '-NoProfile', '-Command', wrapper]
            options['env'] = env
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


def test_cache_tamper_and_lease_conflict() -> None:
    if os.name != 'nt' or shutil.which('pwsh') is None:
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
    if os.name != 'nt' or shutil.which('pwsh') is None:
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
    if os.name != 'nt' or shutil.which('pwsh') is None:
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


def main() -> None:
    test_source_selection_and_capacity()
    test_cache_tamper_and_lease_conflict()
    test_hardlink_isolation_cleanup_and_protection()
    test_prepare_failure_retains_without_blocking_next_batch()
    if os.name == 'nt' and shutil.which('pwsh') is not None:
        print('PASS: shared Eden resources, isolation, cache/lease/capacity gates and owned cleanup')
    else:
        print('PASS: source selection/capacity; SKIP: native NTFS isolation/cleanup requires Windows and pwsh')


if __name__ == '__main__':
    main()
