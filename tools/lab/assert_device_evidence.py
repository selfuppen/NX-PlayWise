#!/usr/bin/env python3
"""Assert machine-checkable properties of pulled standard-package evidence.

Consumes the device-reports/standard tree produced by device_lab_roundtrip.py
(logs/<date>/{ipc-client.log,pctl_debug.jsonl,events.jsonl}, results/*.json,
ledger/used_nonces.jsonl, support/recent-events.jsonl) and checks the subset of
the standard-package acceptance checklist that is machine-decidable.

This is an ADVISORY gate layered on top of the authoritative campaign gate
(verify_device_qualification.py). It never fabricates a human observation and
deliberately does not touch restriction visibility, game behaviour, button
holds, dock replug, or clock changes -- those stay with the operator.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys

SECRET_SUBSTRINGS = ("grant_secret", "pin_hash", "pin_salt", "token_signature")
EIGHT_DIGIT = re.compile(r"(?<!\d)\d{8}(?!\d)")
IPC_KEYS = ("ts", "client", "stage", "rc", "module", "description", "version", "expected")


@dataclass
class Finding:
    check: str
    status: str  # PASS | FAIL | WARN | INFO
    detail: str


def _iter_jsonl(path: Path):
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line:
            continue
        try:
            yield json.loads(line)
        except json.JSONDecodeError:
            yield None


def _log_dirs(evidence: Path) -> list[Path]:
    logs = evidence / "logs"
    return sorted(p for p in logs.glob("*") if p.is_dir()) if logs.is_dir() else []

def check_pctl_debug(evidence: Path) -> list[Finding]:
    start_hits = 0
    apply_hits = 0
    files = 0
    for day_dir in _log_dirs(evidence):
        debug = day_dir / "pctl_debug.jsonl"
        if not debug.is_file():
            continue
        files += 1
        for record in _iter_jsonl(debug):
            if not isinstance(record, dict):
                continue
            stage = str(record.get("stage", ""))
            if "start_timer" in stage:
                start_hits += 1
            if stage == "apply_target":
                apply_hits += 1
    if files == 0:
        return [Finding("pctl_debug_present", "WARN", "no pctl_debug.jsonl pulled; cannot check step 13")]
    timer = Finding("no_unexpected_timer_start",
                    "FAIL" if start_hits else "PASS",
                    f"{start_hits} record(s) carry a start_timer stage (step 13 regression)"
                    if start_hits else "no start_timer stage recorded (step 13)")
    return [timer, Finding("enforce_apply_target", "INFO", f"{apply_hits} apply_target enforce record(s)")]


def check_nonce_ledger(evidence: Path) -> list[Finding]:
    path = evidence / "ledger" / "used_nonces.jsonl"
    if not path.is_file():
        return [Finding("nonce_single_consume", "WARN", "no used_nonces.jsonl pulled")]
    seen: set[tuple[int, int, int]] = set()
    dups = 0
    total = 0
    for record in _iter_jsonl(path):
        if not isinstance(record, dict) or "nonce" not in record:
            continue
        key = (int(record.get("day_index", -1)), int(record["nonce"]), int(record.get("token_version", 1)))
        total += 1
        if key in seen:
            dups += 1
        else:
            seen.add(key)
    if dups:
        return [Finding("nonce_single_consume", "FAIL",
                        f"{dups} duplicate nonce consumption(s) across {total} ledger entries")]
    return [Finding("nonce_single_consume", "PASS", f"{total} nonce(s) each consumed once")]

def _parse_ipc_line(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.split():
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


def check_ipc_log(evidence: Path) -> list[Finding]:
    files = 0
    malformed = 0
    dup_consecutive = 0
    overlay_connected = False
    for day_dir in _log_dirs(evidence):
        log = day_dir / "ipc-client.log"
        if not log.is_file():
            continue
        files += 1
        prev_key = None
        for raw in log.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip()
            if not line:
                continue
            fields = _parse_ipc_line(line)
            if any(key not in fields for key in IPC_KEYS):
                malformed += 1
                continue
            if fields.get("client") == "overlay" and fields.get("stage") == "connected":
                overlay_connected = True
            key = (fields.get("client"), fields.get("stage"), fields.get("rc"), fields.get("version"))
            if key == prev_key:
                dup_consecutive += 1
            prev_key = key
    if files == 0:
        return [Finding("ipc_log_present", "WARN", "no ipc-client.log pulled")]
    return [
        Finding("ipc_log_well_formed", "FAIL" if malformed else "PASS",
                f"{malformed} malformed line(s)" if malformed else "every line carries all 8 fields"),
        Finding("ipc_log_dedup", "WARN" if dup_consecutive else "PASS",
                f"{dup_consecutive} consecutive duplicate failure line(s)" if dup_consecutive
                else "no consecutive duplicate failures"),
        Finding("ipc_overlay_connected", "INFO",
                "overlay reconnect logged" if overlay_connected else "no overlay connected line seen"),
    ]

def check_redaction(evidence: Path) -> list[Finding]:
    secret_hits: list[str] = []
    digit_hits: list[str] = []

    def scan(path: Path, allow_digit_scan: bool) -> None:
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in SECRET_SUBSTRINGS:
            if marker in text:
                secret_hits.append(f"{path.name}:{marker}")
        if allow_digit_scan and EIGHT_DIGIT.search(text):
            digit_hits.append(path.name)

    for day_dir in _log_dirs(evidence):
        ipc = day_dir / "ipc-client.log"
        if ipc.is_file():
            scan(ipc, allow_digit_scan=False)  # rc=0x00000000 is not a code
        events = day_dir / "events.jsonl"
        if events.is_file():
            scan(events, allow_digit_scan=True)
    recent = evidence / "support" / "recent-events.jsonl"
    if recent.is_file():
        scan(recent, allow_digit_scan=True)

    return [
        Finding("log_secret_redaction", "FAIL" if secret_hits else "PASS",
                "; ".join(sorted(set(secret_hits))) if secret_hits else "no secret markers in diagnostic logs"),
        Finding("log_eight_digit_scan", "WARN" if digit_hits else "PASS",
                f"stray 8-digit pattern in {sorted(set(digit_hits))}" if digit_hits
                else "no stray 8-digit codes in event logs"),
    ]


def check_results(evidence: Path) -> list[Finding]:
    results = evidence / "results"
    if not results.is_dir():
        return [Finding("result_state", "WARN", "no results/ pulled")]
    files = sorted(results.glob("*.json"))
    errors = 0
    for result_file in files:
        try:
            data = json.loads(result_file.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        reason = str(data.get("error", "")) if isinstance(data, dict) else ""
        if reason and reason not in ("", "none", "ok"):
            errors += 1
    return [Finding("result_state", "WARN" if errors else "INFO",
                    f"{len(files)} result file(s), {errors} carrying an error reason")]

def assess_evidence(evidence: Path) -> list[Finding]:
    findings: list[Finding] = []
    findings += check_pctl_debug(evidence)
    findings += check_ipc_log(evidence)
    findings += check_redaction(evidence)
    findings += check_nonce_ledger(evidence)
    findings += check_results(evidence)
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Assert machine-checkable PlayWise device evidence.")
    parser.add_argument("--evidence", type=Path, default=Path("device-reports") / "standard",
                        help="Pulled standard-package evidence dir (default device-reports/standard)")
    parser.add_argument("--output", type=Path, default=Path("build") / "qualification" / "standard-assertions.json")
    args = parser.parse_args(argv)
    if not args.evidence.exists():
        print(f"FAIL: evidence dir not found: {args.evidence}", file=sys.stderr)
        return 1
    findings = assess_evidence(args.evidence)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema_version": 1,
        "evidence": str(args.evidence.resolve()),
        "findings": [finding.__dict__ for finding in findings],
    }
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    failed = 0
    for finding in findings:
        print(f"{finding.status}: [{finding.check}] {finding.detail}")
        if finding.status == "FAIL":
            failed += 1
    print(f"assert_device_evidence: {failed} failing check(s); report {args.output}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
