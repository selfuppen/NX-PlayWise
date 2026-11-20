from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.automation.eden_refresh_flow import REFRESH_SCENARIOS, STAGES, validate_stages, verify_refresh
from tools.automation.eden_runner import GateError, digest, save, scenario_names


def fixture() -> tuple[dict, dict]:
    receipts = {name: {"request_id": name, "type": "status", "status": "ok", "completed_at": stamp}
                for name, stamp in (("old", 1000), ("manual", 1001), ("expired", 1125))}
    stages = {}
    ticks = [1, 2, 3, 4, 127, 128]
    for index, name in enumerate(STAGES):
        ids = ["old"] if index < 2 else ["manual", "old"] if index < 5 else ["expired", "manual", "old"]
        stages[name] = {"host_monotonic_ns": ticks[index] * 1_000_000_000,
            "result_ids": ids, "instance": "abcd", "sequence": index + 1,
            "rules_sha256": "1" * 64, "config_sha256": "2" * 64, "ledger_sha256": None,
            "ui": {"view": 1, "overlay": 0, "waiting": False, "restricted_now": 1,
                   "today_minutes": 1, "parent_page": 1 if name.startswith("idle_") else 0}}
    return stages, receipts


def rejected(stages: dict, receipts: dict) -> None:
    try:
        validate_stages(stages, receipts)
    except GateError:
        return
    raise AssertionError("invalid refresh evidence accepted")


def test_evidence_gates() -> None:
    stages, receipts = fixture()
    validate_stages(stages, receipts)
    encoded = json.dumps({"stages": stages, "receipts": receipts})
    reread = json.loads(encoded)
    for _ in range(2):
        validate_stages(reread["stages"], reread["receipts"])
    for stage, field, value in (
        ("cached", "result_ids", ["manual", "old"]),
        ("manual", "result_ids", ["old"]),
        ("idle_end", "result_ids", ["expired", "manual", "old"]),
        ("expired", "result_ids", ["manual", "old"]),
        ("expired", "result_ids", ["manual", "old", "unknown"]),
        ("cached", "rules_sha256", "3" * 64),
        ("cached", "config_sha256", "4" * 64),
        ("cached", "ledger_sha256", "5" * 64),
        ("cached", "instance", "foreign"),
        ("cached", "sequence", True),
        ("cached", "host_monotonic_ns", 1),
        ("idle_end", "host_monotonic_ns", 125_000_000_000),
    ):
        bad = deepcopy(stages)
        bad[stage][field] = value
        rejected(bad, receipts)
    for field, value in (("request_id", "other"), ("type", "set_today_limit"),
                         ("status", "error"), ("completed_at", True), ("completed_at", 1050)):
        bad = deepcopy(receipts)
        bad["expired"][field] = value
        rejected(stages, bad)
    bad = deepcopy(stages)
    extra = deepcopy(receipts)
    extra["duplicate"] = {**receipts["expired"], "request_id": "duplicate"}
    bad["expired"]["result_ids"].append("duplicate")
    rejected(bad, extra)
    for field, value in (("waiting", True), ("overlay", 3), ("view", 0),
                         ("parent_page", 1), ("today_minutes", 120), ("restricted_now", 0)):
        bad = deepcopy(stages)
        bad["cached"]["ui"][field] = value
        rejected(bad, receipts)
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder)
        for name in REFRESH_SCENARIOS:
            save(output / f"assertions/{name}.json", {"passed": True})
        try:
            verify_refresh(output)
        except (OSError, GateError):
            pass
        else:
            raise AssertionError("positive flags without original evidence accepted")
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder)
        stages, receipts = fixture()
        stream_rows = []
        for name, row in stages.items():
            for field in ("rules", "config"):
                raw = (field + ' fixture\n').encode('ascii')
                row[f"{field}_sha256"] = hashlib.sha256(raw).hexdigest()
                path = output / f"refresh-evidence/{name}-{field}.raw"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(raw)
            save(output / f"refresh-evidence/{name}.json", row)
            observed = {key: row[key] for key in ("instance", "sequence", "ui")}
            save(output / f"refresh-evidence/{name}-ui.json", observed)
            stream_rows.append(observed)
        for name, row in receipts.items():
            save(output / f"refresh-evidence/results/{name}.json", row)
        raw_stream = output / 'app-final/support/automation-ui.jsonl'
        raw_stream.parent.mkdir(parents=True)
        raw_stream.write_text(''.join(json.dumps(row) + '\n' for row in stream_rows), encoding='utf-8')
        save(output / 'checkpoints/refresh-before-reopen.json', {'instance': 'abcd'})
        save(output / 'checkpoints/refresh_reopen.json', {'instance': 'bcde', 'ui': {
            'view': 0, 'today_minutes': 120, 'restricted_now': 0}})
        save(output / 'app-final/rules.json', {'today_override_present': True,
            'today_override_mode': 'limit', 'today_override_minutes': 120})
        save(output / 'assertions/refresh_reopen.json', {'rules_sha256': digest(output / 'app-final/rules.json')})
        for request_id in ('limit', 'restore'):
            save(output / f'receipts/{request_id}.json', {'request_id': request_id,
                'type': 'set_today_limit', 'status': 'ok'})
        verify_refresh(output)
        verify_refresh(output)
        raw_stream.write_text(''.join(json.dumps(row) + '\n' for row in stream_rows[:-1]), encoding='utf-8')
        try:
            verify_refresh(output)
        except GateError:
            pass
        else:
            raise AssertionError('missing original UI observation accepted')
    print("PASS: navigation refresh inventories, clocks, business preservation and failure evidence")


def main() -> None:
    assert scenario_names("refresh") == REFRESH_SCENARIOS
    test_evidence_gates()


if __name__ == "__main__":
    main()
