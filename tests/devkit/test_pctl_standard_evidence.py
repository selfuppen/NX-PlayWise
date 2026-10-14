from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import pctl_standard_evidence as evidence
import pctl_usage_qualification as usage
import test_qualification_tools as fixtures
from test_pctl_usage_qualification import set_contract


CONFIG = {"schema_version": 1, "run_id": "0" * 32, "commit": "a" * 40,
          "release_id": "playwise-test+aaaaaaaaaaaa", "duration_s": 300}


def raw_rows() -> list[dict]:
    start = {"schema_version": 1, "kind": "usage_evidence_start", **{k: CONFIG[k] for k in
             ("run_id", "commit", "release_id")}, "boot_id": "1" * 16,
             "started_ns": "1", "duration_ns": "300000000000", "interval_ns": "5000000000"}
    sample = {"schema_version": 1, "kind": "usage_evidence_sample", "source": "pctl_1952",
              "scope": "console_usable_time", "run_id": CONFIG["run_id"], "boot_id": "1" * 16,
              "valid": True, "begin_ns": "1000000000", "end_ns": "1100000000",
              "unix_before": 1791914860, "unix_after": 1791914860, "day_before": 2478, "day_after": 2478,
              "clock_before_valid": True, "clock_after_valid": True, "start_timer_calls": "2",
              **{key: 0 for key in evidence.RESULT_FIELDS}, "timer_enabled": True, "restricted": True,
              "temporarily_unlocked": False, "signed_remaining_ns": "-591000000000",
              "total_ns": "4191000000000", "settings_hex": "01" * 68}
    return [start, sample, {"schema_version": 1, "kind": "usage_evidence_stop", "reason": "lease_expired",
                            "samples": 1, "consecutive_failures": 0}]


def encode(rows: list[dict]) -> bytes:
    return ("\n".join(json.dumps(r) for r in rows) + "\n").encode()


def rejected(rows: list[dict], config: dict = CONFIG) -> None:
    try:
        evidence.audit(encode(rows), config)
    except (usage.UsageQualificationError, evidence.qualification.QualificationError):
        return
    raise AssertionError("malformed or misbound raw accepted")


def test_audit() -> None:
    data = encode(raw_rows())
    result = evidence.audit(data, CONFIG)
    assert result["qualified"] is False and result["stop_record_present"]
    assert result["raw_sha256"] == hashlib.sha256(data).hexdigest()
    assert result["samples"][0]["raw"]["signed_remaining_ns"] == "-591000000000"
    tail = encode(raw_rows()[:2]) + b'{"unclosed":'
    result = evidence.audit(tail, CONFIG)
    assert result["partial_tail_lines"] == [3] and not result["stop_record_present"]
    for field in evidence.RESULT_FIELDS:
        rows = raw_rows(); rows[1][field] = 777; rows[1]["valid"] = False
        assert evidence.audit(encode(rows), CONFIG)["invalid_sample_lines"] == [2]
        rows[1]["valid"] = True; rejected(rows)
    for key, value in (("boot_id", "2" * 16), ("run_id", "3" * 32), ("timer_result", False),
                       ("settings_hex", "01"), ("day_after", 2479), ("total_ns", "-1"),
                       ("end_ns", "3100000001"), ("clock_after_valid", False)):
        rows = raw_rows(); rows[1][key] = value; rejected(rows)
    rows = raw_rows(); rows[0]["commit"] = "b" * 40; rejected(rows)
    rows = raw_rows(); rows[2]["samples"] = 2; rejected(rows)
    rows = raw_rows(); rows.append(deepcopy(rows[1])); rejected(rows)
    rows = raw_rows(); rows.insert(2, deepcopy(rows[1])); rows[-1]["samples"] = 2; rejected(rows)
    rows = raw_rows(); later = deepcopy(rows[1]); later.update(begin_ns="6000000000", end_ns="6100000000", start_timer_calls="1")
    rows.insert(2, later); rows[-1]["samples"] = 2; rejected(rows)
    try:
        evidence.audit(data.replace(b'"valid": true', b'"valid": true, "valid": true'), CONFIG)
    except evidence.qualification.QualificationError:
        pass
    else:
        raise AssertionError("duplicate raw key accepted")


def test_prepare_and_collect() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        packages = fixtures.prepare_packages(root); set_contract(packages)
        out = root / "batch"
        plan = evidence.prepare(packages, out, 300)
        assert plan["manual_observations"] is None and plan["report_status"] == "prepared_not_deployed"
        assert len(plan["config"]["run_id"]) == 32
        try:
            evidence.prepare(packages, out, 300)
        except usage.UsageQualificationError:
            pass
        else:
            raise AssertionError("overwrote existing batch")
        for duration in (True, 299, 7201):
            try:
                evidence.prepare(packages, root / "bad", duration)
            except usage.UsageQualificationError:
                pass
            else:
                raise AssertionError("unbounded lease accepted")
        rows = raw_rows(); rows[0].update({k: plan["config"][k] for k in ("run_id", "commit", "release_id")})
        rows[1]["run_id"] = plan["config"]["run_id"]
        data = encode(rows)
        class Device:
            def __enter__(self): return self
            def __exit__(self, *args): return None
            def ftp(self): return self
            def nlst(self, path):
                assert path == evidence.REMOTE
                return [f'usage-evidence-{plan["config"]["run_id"]}-{"1" * 16}.jsonl', "unrelated.jsonl"]
            def read(self, path):
                assert path.startswith(evidence.REMOTE + "/usage-evidence-")
                return data
        first = evidence.collect(Device(), out, plan)
        assert len(first) == 1 and first[0].read_bytes() == data
        assert evidence.collect(Device(), out, plan) == first
        before = first[0].read_bytes()
        data += b'{"partial"'
        second = evidence.collect(Device(), out, plan)
        assert second[0] != first[0] and first[0].read_bytes() == before
        assert usage.read_json(second[0].with_suffix(".audit.json"))["partial_tail_lines"] == [4]


if __name__ == "__main__":
    test_audit()
    test_prepare_and_collect()
    print("PASS production evidence plan, raw identity/errors, signed precision and immutable read-only pulls")
