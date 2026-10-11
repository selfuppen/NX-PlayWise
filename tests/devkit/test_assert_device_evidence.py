#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import assert_device_evidence as ade  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _status(findings: list[ade.Finding], check: str) -> str | None:
    for finding in findings:
        if finding.check == check:
            return finding.status
    return None


def build_clean(evidence: Path) -> None:
    day = evidence / "logs" / "2026-10-11"
    _write(day / "pctl_debug.jsonl", '{"stage":"apply_target","mode":"enforce"}\n')
    _write(
        day / "ipc-client.log",
        "ts=1760000000 client=overlay stage=connected rc=0x00000000 "
        "module=0 description=0 version=1 expected=1\n",
    )
    _write(day / "events.jsonl", '{"event":"status_ok"}\n')
    _write(evidence / "ledger" / "used_nonces.jsonl",
           '{"day_index":2380,"nonce":4660}\n{"day_index":2380,"nonce":4661}\n')
    _write(evidence / "results" / "r1.json", '{"type":"status","error":"none"}\n')

def test_clean_evidence_has_no_failures() -> None:
    with tempfile.TemporaryDirectory(prefix="ade-clean-") as tmp:
        evidence = Path(tmp) / "standard"
        build_clean(evidence)
        findings = ade.assess_evidence(evidence)
        failures = [f for f in findings if f.status == "FAIL"]
        require(not failures, f"clean evidence must not fail: {[f.check for f in failures]}")
        require(_status(findings, "no_unexpected_timer_start") == "PASS", "clean logs prove no start_timer")
        require(_status(findings, "nonce_single_consume") == "PASS", "distinct nonces must pass")
        rc = ade.main(["--evidence", str(evidence), "--output", str(Path(tmp) / "out.json")])
        require(rc == 0, "clean evidence must exit 0")


def test_start_timer_stage_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="ade-timer-") as tmp:
        evidence = Path(tmp) / "standard"
        build_clean(evidence)
        _write(evidence / "logs" / "2026-10-11" / "pctl_debug.jsonl",
               '{"stage":"apply_target"}\n{"stage":"start_timer_fallback"}\n')
        findings = ade.assess_evidence(evidence)
        require(_status(findings, "no_unexpected_timer_start") == "FAIL",
                "a start_timer stage must fail step 13")


def test_secret_in_event_log_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="ade-secret-") as tmp:
        evidence = Path(tmp) / "standard"
        build_clean(evidence)
        _write(evidence / "logs" / "2026-10-11" / "events.jsonl",
               '{"event":"leak","grant_secret":"should-never-appear"}\n')
        findings = ade.assess_evidence(evidence)
        require(_status(findings, "log_secret_redaction") == "FAIL", "a leaked secret must fail redaction")
        rc = ade.main(["--evidence", str(evidence), "--output", str(Path(tmp) / "out.json")])
        require(rc == 1, "leaked secret must exit 1")


def test_duplicate_nonce_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="ade-nonce-") as tmp:
        evidence = Path(tmp) / "standard"
        build_clean(evidence)
        _write(evidence / "ledger" / "used_nonces.jsonl",
               '{"day_index":2380,"nonce":4660}\n{"day_index":2380,"nonce":4660}\n')
        findings = ade.assess_evidence(evidence)
        require(_status(findings, "nonce_single_consume") == "FAIL", "a replayed nonce must fail")


def main() -> int:
    test_clean_evidence_has_no_failures()
    test_start_timer_stage_fails()
    test_secret_in_event_log_fails()
    test_duplicate_nonce_fails()
    print("Assert device evidence tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
