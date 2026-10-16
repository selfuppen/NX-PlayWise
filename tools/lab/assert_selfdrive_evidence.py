#!/usr/bin/env python3
"""Assert machine-decidable properties of the lab self-drive evidence stream.

Consumes the advisory lab/selfdrive/ tree produced on-device by the self-driver
(pulled by device_lab_roundtrip.py's existing `lab` mapping into
device-reports/device-lab/lab/selfdrive/). It checks only what a machine can
decide from a self-driven run -- exact PCTL restore, no unexpected 0x44 offsets,
and that the run never fabricated a human observation. Sleep-exclusion and
restriction *visibility* are deliberately NOT asserted here: they need the
physical world and stay with the human operator (真机兜底).

This is ADVISORY and never promoted; it layers under the authoritative
verify_device_qualification.py, which only ever reads human-attested reports/.
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


@dataclass
class Finding:
    check: str
    status: str  # PASS | FAIL | WARN | INFO
    detail: str


def _read_json(path: Path) -> dict | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def select_report(evidence: Path) -> tuple[Path | None, dict | None]:
    """Prefer the run named by summary.json; else the newest report file."""
    summary = _read_json(evidence / "summary.json")
    if summary and summary.get("run_id"):
        for suffix in (".selfdrive.json", ".draft.json"):
            candidate = evidence / f"{summary['run_id']}{suffix}"
            if candidate.is_file():
                return candidate, summary
    reports = sorted(evidence.glob("*.selfdrive.json")) + sorted(evidence.glob("*.draft.json"))
    if reports:
        newest = max(reports, key=lambda p: p.stat().st_mtime)
        return newest, summary
    return None, summary
def assess(evidence: Path) -> list[Finding]:
    findings: list[Finding] = []
    report_path, summary = select_report(evidence)
    if report_path is None:
        findings.append(Finding("selfdrive_present", "WARN",
            "no *.selfdrive.json/.draft.json pulled from lab/selfdrive/"))
        return findings
    report = _read_json(report_path) or {}
    text = report_path.read_text(encoding="utf-8", errors="replace")

    advisory = report.get("self_driven") is True and report.get("advisory") is True
    findings.append(Finding("advisory_markers", "PASS" if advisory else "FAIL",
        "self_driven+advisory set" if advisory else "report missing self_driven/advisory markers"))

    campaign = report.get("campaign")
    manual = report.get("manual_observation")
    fabricated = (campaign is not None) or (manual == "restriction_visible")
    findings.append(Finding("no_human_attestation", "FAIL" if fabricated else "PASS",
        f"campaign={campaign!r} manual_observation={manual!r} -- self-drive must never attest"
        if fabricated else f"no fabricated human attestation (manual_observation={manual!r})"))

    restoration = report.get("restoration") if isinstance(report.get("restoration"), dict) else {}
    restored = restoration.get("proved") is True and restoration.get("verdict") == "exact_restore_proved"
    findings.append(Finding("exact_restore", "PASS" if restored else "FAIL",
        "0x44 settings exactly restored" if restored else f"restore not proved: {restoration}"))
    phases = [p for p in report.get("phases", []) if isinstance(p, dict)]
    write_phases = 0
    bad_offsets = 0
    for phase in phases:
        scope = phase.get("settings_write_scope", {})
        if isinstance(scope, dict) and scope.get("comparison") == "phase_prewrite_to_after":
            write_phases += 1
            if scope.get("unexpected_bytes_unchanged") is not True:
                bad_offsets += 1
    if write_phases == 0:
        findings.append(Finding("settings_offsets", "INFO", "no settings-write phases in this run"))
    else:
        findings.append(Finding("settings_offsets", "FAIL" if bad_offsets else "PASS",
            f"{bad_offsets}/{write_phases} settings-write phase(s) touched unexpected 0x44 offsets"
            if bad_offsets else f"{write_phases} settings-write phase(s), no unexpected 0x44 offsets"))

    tab = report.get("timer_activation_ab") if isinstance(report.get("timer_activation_ab"), dict) else {}
    findings.append(Finding("activation_evidence", "INFO",
        f"home_awake_counted={tab.get('home_awake_counted')} sleep_excluded={tab.get('sleep_excluded')} "
        "(sleep exclusion needs real sleep; not asserted)"))
    findings.append(Finding("report_status", "INFO",
        f"report_status={report.get('report_status')} ({report_path.name})"))

    verdict = summary.get("verdict") if summary else None
    findings.append(Finding("selfdrive_verdict", "PASS" if verdict == "completed" else "WARN",
        f"verdict={verdict!r} steps={summary.get('steps') if summary else None}"))

    secret_hits = [marker for marker in SECRET_SUBSTRINGS if marker in text]
    findings.append(Finding("log_secret_redaction", "FAIL" if secret_hits else "PASS",
        ", ".join(secret_hits) if secret_hits else "no secret markers in self-drive report"))
    findings.append(Finding("eight_digit_scan", "WARN" if EIGHT_DIGIT.search(text) else "PASS",
        "stray 8-digit pattern in self-drive report" if EIGHT_DIGIT.search(text) else "no stray 8-digit codes"))
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Assert advisory lab self-drive evidence.")
    parser.add_argument("--evidence", type=Path,
                        default=Path("device-reports") / "device-lab" / "lab" / "selfdrive",
                        help="pulled lab/selfdrive dir (default device-reports/device-lab/lab/selfdrive)")
    parser.add_argument("--output", type=Path,
                        default=Path("build") / "qualification" / "selfdrive-assertions.json")
    args = parser.parse_args(argv)
    if not args.evidence.exists():
        print(f"FAIL: evidence dir not found: {args.evidence}", file=sys.stderr)
        return 1
    findings = assess(args.evidence)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = {"schema_version": 1, "advisory": True, "evidence": str(args.evidence.resolve()),
               "findings": [finding.__dict__ for finding in findings]}
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    failed = 0
    for finding in findings:
        print(f"{finding.status}: [{finding.check}] {finding.detail}")
        if finding.status == "FAIL":
            failed += 1
    print(f"assert_selfdrive_evidence: {failed} failing check(s); report {args.output}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())

