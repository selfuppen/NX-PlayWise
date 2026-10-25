from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.device import ui_status_trace as trace
from test_pctl_standard_evidence import raw_rows

MANIFEST = {"profile": "release", "commit": "a" * 40, "release_id": "playwise-test+aaaaaaaaaaaa",
            "build": {"source_dirty": False}, "ui_status_trace_version": 1}
RUNTIME = {"profile": "release", "release_id": MANIFEST["release_id"], "boot_id": "b" * 16}
SUBJECT = {"commit": MANIFEST["commit"], "release_id": MANIFEST["release_id"], "backend_boot_id": RUNTIME["boot_id"]}
RUN = "c" * 32


def rows(client: str = "nro", origin: str = "resume_and_focus", qid: str = "1001-abcd") -> list[dict]:
    return [{"schema_version": 1, "kind": "ui_status_trace_start", "run_id": RUN, **SUBJECT,
             "client": client, "started_mono_ns": "1000000000", "expires_mono_ns": "301000000000", "max_records": 32},
            {"schema_version": 1, "kind": "ui_status_trace_request", "sequence": 1, "request_id": qid,
             "origin": origin, "monotonic_ns": "2000000000", "focus_state": 1 if client == "nro" else -1, "transport_status": 0},
            {"schema_version": 1, "kind": "ui_status_trace_stop", "reason": "client_exit", "records": 1,
             "monotonic_ns": "3000000000"}]


def encoded(value: list[dict]) -> bytes:
    return b"".join(json.dumps(x).encode() + b"\n" for x in value)


def reject(call) -> None:
    try:
        call()
    except (ValueError, RuntimeError):
        return
    raise AssertionError("invalid evidence accepted")


def pair(qid: str) -> tuple[dict, dict]:
    return ({"request_id": qid, "type": "status"}, {"request_id": qid, "type": "status", "status": "ok"})


def main() -> None:
    sample = raw_rows()[1]
    sample["boot_id"] = RUNTIME["boot_id"]
    config = trace.prepare_config(MANIFEST, RUNTIME, sample, RUN)
    assert config["expires_mono_ns"] == str(int(sample["end_ns"]) + 300_000_000_000)
    assert set(config) == {"schema_version", "run_id", "commit", "release_id", "backend_boot_id", "expires_mono_ns", "max_records"}
    for duration in (0, 901, True):
        reject(lambda: trace.prepare_config(MANIFEST, RUNTIME, sample, RUN, duration))
    bad = deepcopy(MANIFEST); bad["build"]["source_dirty"] = True
    reject(lambda: trace.prepare_config(bad, RUNTIME, sample, RUN))
    bad = deepcopy(MANIFEST); bad.pop("ui_status_trace_version")
    reject(lambda: trace.prepare_config(bad, RUNTIME, sample, RUN))
    bad_runtime = dict(RUNTIME, boot_id="0" * 16)
    reject(lambda: trace.prepare_config(MANIFEST, bad_runtime, sample, RUN))
    original = rows()
    nro = trace.audit_trace(encoded(original), SUBJECT, RUN)
    overlay = trace.audit_trace(encoded(rows("overlay", "button_y", "2000-0001")), SUBJECT, RUN)
    countdown = trace.audit_trace(encoded(rows("overlay", "countdown_end", "3000-0001")), SUBJECT, RUN)
    joined = trace.join_receipts([pair("1001-abcd"), pair("2000-0001"), pair("3000-0001")],
        [nro, overlay, countdown])
    assert joined["matching_ok_count"] == joined["attributed_ok_count"] == 3
    assert joined["all_matching_ok_attributed"] and joined["records"][0]["provenance"]["origin"] == "resume_and_focus"
    assert joined["records"][1]["provenance"]["client"] == "overlay"
    assert joined["records"][2]["provenance"]["origin"] == "countdown_end"
    assert not joined["qualified"] and not joined["input_isolation_proved"] and not joined["game_effects_proved"]
    missing = trace.join_receipts([pair("unknown")], [nro])
    assert missing["records"][0]["provenance"] is None and not missing["all_matching_ok_attributed"]
    reject(lambda: trace.join_receipts([pair("1001-abcd")], [nro, nro]))
    failed = deepcopy(nro); failed["records"][0]["transport_status"] = 4
    reject(lambda: trace.join_receipts([pair("1001-abcd")], [failed]))
    mutations = [
        (0, "commit", "0" * 40), (0, "backend_boot_id", "0" * 16), (0, "client", "host"),
        (0, "max_records", True), (0, "expires_mono_ns", "999999999999"),
        (1, "sequence", 2), (1, "monotonic_ns", "0"), (1, "monotonic_ns", "301000000000"),
        (1, "focus_state", True), (1, "origin", "guess"), (1, "request_id", 'private"value'),
        (2, "records", 0), (2, "reason", "invalid_record"), (2, "reason", "lease_expired"),
    ]
    for index, key, value in mutations:
        bad = deepcopy(original); bad[index][key] = value
        reject(lambda: trace.audit_trace(encoded(bad), SUBJECT, RUN))
    reject(lambda: trace.audit_trace(encoded(original)[:-1], SUBJECT, RUN))
    reject(lambda: trace.audit_trace(encoded(original[:-1]), SUBJECT, RUN))
    bad = deepcopy(original); bad[1]["payload"] = "must-not-be-accepted"
    reject(lambda: trace.audit_trace(encoded(bad), SUBJECT, RUN))
    duplicate = encoded(original).replace(b'"max_records": 32', b'"max_records": 32, "max_records": 32')
    reject(lambda: trace.audit_trace(duplicate, SUBJECT, RUN))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        out = root / "audit.json"
        trace.save_or_verify(out, joined); first = out.read_bytes()
        trace.save_or_verify(out, joined); assert out.read_bytes() == first
        reject(lambda: trace.save_or_verify(out, missing)); assert out.read_bytes() == first
        files = {"manifest.json": MANIFEST, "runtime.json": RUNTIME, "sample.json": sample}
        for name, value in files.items(): (root / name).write_text(json.dumps(value), encoding="utf-8")
        args = [sys.executable, str(ROOT / "tools/device/ui_status_trace.py"), "prepare", "--manifest", str(root / "manifest.json"),
                "--runtime", str(root / "runtime.json"), "--sample", str(root / "sample.json"), "--run-id", RUN,
                "--out", str(root / "config.json")]
        subprocess.run(args, check=True, capture_output=True)
        subprocess.run(args, check=True, capture_output=True)
        assert json.loads((root / "config.json").read_bytes()) == config
        receipt_dir = root / "receipts"; receipt_dir.mkdir()
        for qid in ("1001-abcd", "2000-0001"):
            q, r = pair(qid)
            (receipt_dir / ("accepted-" + qid + ".json")).write_text(json.dumps(q), encoding="utf-8")
            (receipt_dir / ("result-" + qid + ".json")).write_text(json.dumps(r), encoding="utf-8")
        (root / "nro.jsonl").write_bytes(encoded(original))
        (root / "overlay.jsonl").write_bytes(encoded(rows("overlay", "button_y", "2000-0001")))
        args = [sys.executable, str(ROOT / "tools/device/ui_status_trace.py"), "audit", "--manifest", str(root / "manifest.json"),
                "--runtime", str(root / "runtime.json"), "--trace", str(root / "nro.jsonl"), "--trace", str(root / "overlay.jsonl"),
                "--receipts", str(receipt_dir), "--run-id", RUN, "--out", str(root / "joined.json")]
        subprocess.run(args, check=True, capture_output=True)
        first = (root / "joined.json").read_bytes()
        subprocess.run(args, check=True, capture_output=True)
        assert first == (root / "joined.json").read_bytes()
    print("PASS: offline UI status config, provenance, ambiguity rejection and repeated CLI/readback")


if __name__ == "__main__":
    main()
