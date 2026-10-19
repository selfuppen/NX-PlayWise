from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.qualification import make_eye_care_qualification_template as eye
from tools.qualification import verify_device_qualification as gate
from tools.qualification import promote_qualified_build as promoter
from tools.lab import capture_device_observations as capture
import test_pctl_direct_qualification as direct
import test_qualification_tools as fixtures
from test_pctl_usage_qualification import set_contract


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def prepare(root: Path):
    packages, reports = fixtures.prepare_packages(root), fixtures.prepare_reports(root)
    set_contract(packages, eye.DIRECT_CONTRACT)
    direct.prepare(packages, reports)
    manifest, _, _, _ = gate.package_identity(packages)
    value = eye.template(manifest['commit'], manifest['release_id'], 'mariko-oled', '22.5.0', '1.11.2', eye.DIRECT_CONTRACT)
    value.update(report_status='final', games=['fixture-a', 'fixture-b'],
                 timing={'rest_start_trigger_seconds': 30, 'rest_end_trigger_seconds': 20},
                 observations={key: True for key, _ in eye.DIRECT_OBSERVATION_FIELDS},
                 pctl={'settings_exactly_restored': True, 'unexpected_raw_offsets': [], 'eye_care_consumed_daily_allowance': False})
    value['pctl_usage_report']['sha256'] = gate.sha256_file(reports / 'pctl-usage-qualification.json')
    return packages, reports, manifest, value


def test_direct_gate_and_promotion() -> None:
    modes = ('ok', 'legacy_schema', 'bool_schema', 'wrong_contract', 'old_proxy_field', 'missing_direct',
             'bad_hash', 'outside_path', 'draft_usage', 'changed_raw', 'wrong_candidate', 'consumed',
             'missing_observations', 'bad_pctl_type', 'wrong_artifact', 'promote_changed_eye', 'promote_missing_binding')
    for mode in modes:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            packages, reports, manifest, value = prepare(root)
            if mode == 'legacy_schema': value = fixtures.eye_care_report_payload()
            if mode == 'bool_schema': value['schema_version'] = True
            if mode == 'wrong_contract': value['usage_contract'] = eye.LEGACY_CONTRACT
            if mode == 'old_proxy_field': value['observations']['unlimited_vs_1440_proxy_compared'] = True
            if mode == 'missing_direct': value['observations'].pop('direct_cycle_observed')
            if mode == 'bad_hash': value['pctl_usage_report']['sha256'] = '0' * 64
            if mode == 'outside_path': value['pctl_usage_report']['path'] = '../pctl-usage-qualification.json'
            if mode == 'draft_usage':
                path = reports / 'pctl-usage-qualification.json'
                data = json.loads(path.read_bytes()); data['report_status'] = 'draft'; write(path, data)
                value['pctl_usage_report']['sha256'] = gate.sha256_file(path)
            if mode == 'changed_raw':
                path = reports / 'direct_cycle.jsonl'; path.write_bytes(path.read_bytes() + b'changed\n')
            if mode == 'wrong_candidate': value['subject']['commit'] = 'b' * 40
            if mode == 'consumed': value['pctl']['eye_care_consumed_daily_allowance'] = True
            if mode == 'missing_observations': value['observations'] = None
            if mode == 'bad_pctl_type': value['pctl'] = []
            write(reports / 'eye-care-qualification.json', value)
            if mode == 'wrong_artifact':
                _, _, _, components = gate.package_identity(packages)
                try:
                    gate.verify_eye_care_report(reports, manifest, 'oled', '22.5.0', '1.11.2', required=True,
                                               standard_sha256='0' * 64, components=components)
                except gate.QualificationError:
                    continue
                raise AssertionError(mode)
            try:
                result = gate.verify(packages, reports, 'oled', '22.5.0', '1.11.2', require_eye_care=True)
            except gate.QualificationError:
                assert mode not in ('ok', 'promote_changed_eye', 'promote_missing_binding'), mode
                continue
            assert mode in ('ok', 'promote_changed_eye', 'promote_missing_binding'), mode
            assert result['eye_care']['sha256'] == gate.sha256_file(reports / 'eye-care-qualification.json')
            if mode == 'promote_changed_eye':
                value['observations']['rest_end_observed'] = False
                write(reports / 'eye-care-qualification.json', value)
            if mode == 'promote_missing_binding': result.pop('eye_care')
            verification = root / 'verification.json'; write(verification, result)
            try:
                promoter.promote(packages, verification, root / 'qualified')
            except promoter.PromotionError:
                assert mode != 'ok' and not (root / 'qualified').exists(), mode
            else:
                assert mode == 'ok', mode


def test_template_and_capture_cli() -> None:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        packages, reports, manifest, value = prepare(root)
        output = root / 'eye-draft.json'
        command = [sys.executable, '-X', 'utf8', str(ROOT / 'tools/qualification/make_eye_care_qualification_template.py'),
                   '--commit', manifest['commit'], '--release-id', manifest['release_id'],
                   '--usage-contract', eye.DIRECT_CONTRACT, '--output', str(output)]
        assert subprocess.run(command, capture_output=True).returncode == 0
        draft = json.loads(output.read_bytes())
        assert draft['schema_version'] == 2 and all(v is None for v in draft['observations'].values())
        assert 'unlimited_vs_1440_proxy_compared' not in draft['observations']
        original = output.read_bytes()
        assert subprocess.run(command, capture_output=True).returncode != 0 and output.read_bytes() == original
        answers = iter(['fixture-a', 'fixture-b', '30', '20'] + ['y'] * len(eye.DIRECT_OBSERVATION_FIELDS) + ['y', 'n'])
        prompts = []
        def reader(prompt):
            prompts.append(prompt)
            return next(answers)
        args = ['eye-care', '--commit', manifest['commit'], '--release-id', manifest['release_id'],
                '--usage-contract', eye.DIRECT_CONTRACT, '--reports', str(reports)]
        with redirect_stdout(io.StringIO()): assert capture.main(args, reader=reader) == 0
        assert not any('临时 1440' in prompt for prompt in prompts)
        assert gate.verify(packages, reports, 'oled', '22.5.0', '1.11.2', require_eye_care=True)['status'] == 'passed'
        original = (reports / 'eye-care-qualification.json').read_bytes()
        with redirect_stdout(io.StringIO()): assert capture.main(args, reader=lambda _: (_ for _ in ()).throw(AssertionError('unexpected prompt'))) == 1
        assert (reports / 'eye-care-qualification.json').read_bytes() == original
        missing = root / 'missing'
        args[-1] = str(missing)
        with redirect_stdout(io.StringIO()): assert capture.main(args, reader=lambda _: (_ for _ in ()).throw(AssertionError('unexpected prompt'))) == 1
        assert not (missing / 'eye-care-qualification.json').exists()
        # Explicit schema2 cannot qualify an old candidate even with every checkbox true.
        legacy = {'commit': manifest['commit'], 'release_id': manifest['release_id']}
        try: gate.verify_eye_care_report(reports, legacy, 'oled', '22.5.0', '1.11.2', required=True)
        except gate.QualificationError: pass
        else: raise AssertionError('schema2 accepted for legacy candidate')


if __name__ == '__main__':
    test_direct_gate_and_promotion()
    test_template_and_capture_cli()
    print('PASS eye-care v2: direct raw binding, legacy isolation, CLI capture and promotion recheck')
