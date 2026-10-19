"""Offline UI status provenance. Never connects, injects input or submits requests."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.device.pctl_standard_evidence import raw_sample
from tools.device.ui_receipt_observer import summarize_receipts

ORIGINS = {"other", "button_y", "resume", "focus", "resume_and_focus", "overlay_ready", "touch"}
STOP_REASONS = {"client_exit", "record_limit", "lease_expired", "config_removed_or_changed", "config_or_runtime_changed"}


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def decode(data: bytes) -> dict:
    def pairs(items: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key")
            result[key] = value
        return result
    value = json.loads(data, object_pairs_hook=pairs,
                       parse_constant=lambda _: (_ for _ in ()).throw(ValueError("non-finite JSON number")))
    require(isinstance(value, dict), "JSON object required")
    return value


def integer(value: object, low: int, high: int) -> int:
    require(type(value) is int and low <= value <= high, "integer out of bounds")
    return value


def ns(value: object) -> int:
    require(isinstance(value, str) and re.fullmatch(r"[1-9][0-9]{0,19}", value) is not None,
            "canonical positive nanoseconds required")
    return integer(int(value), 1, (1 << 64) - 1)


def identity(manifest: dict, runtime: dict) -> dict:
    require(manifest.get("profile") == runtime.get("profile") == "release" and
            manifest.get("build", {}).get("source_dirty") is False and
            type(manifest.get("ui_status_trace_version")) is int and manifest["ui_status_trace_version"] == 1,
            "clean production identity with UI status trace v1 required")
    commit, release, boot = manifest.get("commit"), manifest.get("release_id"), runtime.get("boot_id")
    require(isinstance(commit, str) and re.fullmatch(r"[0-9a-f]{40}", commit) is not None and
            isinstance(release, str) and re.fullmatch(r"[A-Za-z0-9_.+\-]{1,127}", release) is not None and
            runtime.get("release_id") == release and isinstance(boot, str) and
            re.fullmatch(r"[0-9a-f]{16}", boot) is not None, "candidate/runtime binding differs")
    return {"commit": commit, "release_id": release, "backend_boot_id": boot}


def prepare_config(manifest: dict, runtime: dict, sample: dict, run_id: str,
                   duration_s: int = 300, max_records: int = 32) -> dict:
    subject = identity(manifest, runtime)
    require(raw_sample(sample) and sample.get("valid") is True and sample.get("boot_id") == runtime["boot_id"],
            "valid current production sample required")
    require(re.fullmatch(r"[0-9a-f]{32}", run_id) is not None, "invalid run id")
    deadline = ns(sample["end_ns"]) + integer(duration_s, 1, 900) * 1_000_000_000
    integer(deadline, 1, (1 << 64) - 1)
    return {"schema_version": 1, "run_id": run_id, **subject,
            "expires_mono_ns": str(deadline), "max_records": integer(max_records, 1, 128)}


def audit_trace(data: bytes, subject: dict, run_id: str) -> dict:
    require(bool(data) and data.endswith(b"\n"), "trace must end on a complete line")
    rows = [decode(line) for line in data.splitlines()]
    require(len(rows) >= 2, "trace start and clean terminal record required")
    start, stop = rows[0], rows[-1]
    require(set(start) == {"schema_version", "kind", "run_id", "commit", "release_id", "backend_boot_id",
                           "client", "started_mono_ns", "expires_mono_ns", "max_records"}, "unexpected start fields")
    require(type(start["schema_version"]) is int and start["schema_version"] == 1 and
            start["kind"] == "ui_status_trace_start" and start["run_id"] == run_id and
            all(start.get(key) == value for key, value in subject.items()) and
            start["client"] in ("nro", "overlay"), "trace candidate/client binding differs")
    require(re.fullmatch(r"[0-9a-f]{32}", run_id) is not None, "invalid run id")
    started, deadline = ns(start["started_mono_ns"]), ns(start["expires_mono_ns"])
    require(0 < deadline - started <= 900_000_000_000, "trace lease invalid")
    cap = integer(start["max_records"], 1, 128)
    records, seen, previous = [], set(), started
    for sequence, row in enumerate(rows[1:-1], 1):
        require(set(row) == {"schema_version", "kind", "sequence", "request_id", "origin", "monotonic_ns",
                             "focus_state", "transport_status"}, "unexpected record fields")
        require(type(row["schema_version"]) is int and row["schema_version"] == 1 and
                row["kind"] == "ui_status_trace_request" and integer(row["sequence"], 1, cap) == sequence,
                "invalid trace sequence")
        qid = row["request_id"]
        require(isinstance(qid, str) and re.fullmatch(r"[A-Za-z0-9_.+\-]{1,79}", qid) is not None and
                qid not in seen and isinstance(row["origin"], str) and row["origin"] in ORIGINS, "invalid or ambiguous original request")
        at = ns(row["monotonic_ns"])
        require(previous <= at < deadline, "trace timestamp outside lease")
        integer(row["focus_state"], -1, 3)
        integer(row["transport_status"], -(1 << 31), (1 << 31) - 1)
        require(start["client"] != "overlay" or row["focus_state"] == -1, "overlay cannot invent applet focus")
        records.append(row); seen.add(qid); previous = at
    require(set(stop) == {"schema_version", "kind", "reason", "records", "monotonic_ns"} and
            type(stop["schema_version"]) is int and stop["schema_version"] == 1 and
            stop["kind"] == "ui_status_trace_stop" and isinstance(stop["reason"], str) and stop["reason"] in STOP_REASONS and
            integer(stop["records"], 0, cap) == len(records) and ns(stop["monotonic_ns"]) >= previous,
            "clean trace stop/count required")
    require(stop["reason"] != "record_limit" or len(records) == cap, "record-limit stop before cap")
    require(stop["reason"] != "lease_expired" or ns(stop["monotonic_ns"]) >= deadline, "early lease stop")
    return {"start": start, "records": records, "stop": stop, "sha256": hashlib.sha256(data).hexdigest()}


def join_receipts(pairs: list[tuple[dict, dict]], traces: list[dict]) -> dict:
    summary = summarize_receipts(pairs)
    sources = {}
    for trace in traces:
        for row in trace["records"]:
            qid = row["request_id"]
            require(qid not in sources, "ambiguous provenance across trace instances")
            sources[qid] = {"client": trace["start"]["client"], "origin": row["origin"],
                            "monotonic_ns": row["monotonic_ns"], "focus_state": row["focus_state"],
                            "transport_status": row["transport_status"], "trace_sha256": trace["sha256"]}
    records = []
    matched = 0
    for row in summary["records"]:
        source = sources.get(row["request_id"]) if row["type"] == "status" else None
        if source is not None and row["status"] == "ok":
            require(source["transport_status"] == 0, "ok receipt conflicts with failed frontend submission")
            matched += 1
        records.append({**row, "provenance": source})
    return {"schema_version": 1, "qualified": False, "matching_ok_count": summary["matching_ok_count"],
            "attributed_ok_count": matched, "all_matching_ok_attributed": matched == summary["matching_ok_count"] > 0,
            "records": records, "input_isolation_proved": False, "game_effects_proved": False}


def save_or_verify(path: Path, value: dict) -> None:
    encoded = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if path.exists():
        require(path.read_bytes() == encoded, "existing output differs; preserve original")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(encoded)
    require(decode(path.read_bytes()) == value, "JSON readback differs")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="offline config only; does not upload or activate")
    check = sub.add_parser("audit", help="offline closed traces joined to original receipts")
    for p in (prep, check):
        p.add_argument("--manifest", type=Path, required=True)
        p.add_argument("--runtime", type=Path, required=True)
        p.add_argument("--run-id", required=True)
        p.add_argument("--out", type=Path, required=True)
    prep.add_argument("--sample", type=Path, required=True)
    prep.add_argument("--duration-s", type=int, default=300)
    prep.add_argument("--max-records", type=int, default=32)
    check.add_argument("--trace", type=Path, action="append", required=True)
    check.add_argument("--receipts", type=Path, required=True)
    args = parser.parse_args()
    manifest, runtime = decode(args.manifest.read_bytes()), decode(args.runtime.read_bytes())
    if args.command == "prepare":
        value = prepare_config(manifest, runtime, decode(args.sample.read_bytes()), args.run_id,
                               args.duration_s, args.max_records)
    else:
        traces = [audit_trace(p.read_bytes(), identity(manifest, runtime), args.run_id) for p in args.trace]
        pairs = [(decode(p.read_bytes()), decode(p.with_name(p.name.replace("accepted-", "result-", 1)).read_bytes()))
                 for p in sorted(args.receipts.glob("accepted-*.json"))]
        require(bool(pairs), "no original receipt pairs")
        value = {**join_receipts(pairs, traces), "subject": identity(manifest, runtime), "run_id": args.run_id,
                 "trace_sources": [{"path": str(p), "sha256": t["sha256"]} for p, t in zip(args.trace, traces)]}
    save_or_verify(args.out, value)
    print("PASS: offline UI status provenance; no device action or qualification")


if __name__ == "__main__":
    main()
