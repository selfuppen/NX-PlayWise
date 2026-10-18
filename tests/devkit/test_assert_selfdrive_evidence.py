#!/usr/bin/env python3
"""Host regression for tools/lab/assert_selfdrive_evidence.py.

Builds advisory self-drive evidence trees in a temp dir and checks the asserter
passes a clean run and FAILS on a fabricated human observation, an unrestored
0x44, or an unexpected settings offset.
"""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import tools.lab.assert_selfdrive_evidence as asd  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


def write_tree(root: Path, *, run_id: str, manual: str, restore_proved: bool,
               offsets_ok: bool, verdict: str) -> None:
    selfdrive = root / "device-lab" / "lab" / "selfdrive"
    selfdrive.mkdir(parents=True, exist_ok=True)
    report = {
        "version": 2, "schema_version": 2, "run_id": run_id, "mode": "restriction_quick",
        "report_status": "final", "self_driven": True, "advisory": True, "campaign": None,
        "phases": [{"phase": "restriction_effect", "settings_write_scope": {
            "comparison": "phase_prewrite_to_after", "unexpected_bytes_unchanged": offsets_ok}}],
        "timer_activation_ab": {"home_awake_counted": None, "sleep_excluded": None, "fallback_cases": []},
        "manual_observation": manual, "manual_runtime_effect": "not_observed",
        "restoration": {"proved": restore_proved,
                        "verdict": "exact_restore_proved" if restore_proved else "restore_not_proved"},
    }
    (selfdrive / f"{run_id}.selfdrive.json").write_text(json.dumps(report), encoding="utf-8")
    summary = {"schema_version": 1, "self_driven": True, "advisory": True, "mode": "restriction_quick",
               "run_id": run_id, "verdict": verdict, "steps": 4, "restored": restore_proved,
               "restore_verdict": report["restoration"]["verdict"], "finished_at": 42}
    (selfdrive / "summary.json").write_text(json.dumps(summary), encoding="utf-8")


def statuses(evidence: Path) -> dict[str, str]:
    return {f.check: f.status for f in asd.assess(evidence)}
def test_clean_run_passes() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_tree(root, run_id="good-run", manual="self_driven_unobserved",
                   restore_proved=True, offsets_ok=True, verdict="completed")
        evidence = root / "device-lab" / "lab" / "selfdrive"
        found = statuses(evidence)
        check(found.get("advisory_markers") == "PASS", "clean run is marked advisory")
        check(found.get("no_human_attestation") == "PASS", "clean run fabricates no human attestation")
        check(found.get("exact_restore") == "PASS", "clean run proves exact restore")
        check(found.get("settings_offsets") == "PASS", "clean run has no unexpected 0x44 offsets")
        check(found.get("selfdrive_verdict") == "PASS", "clean run verdict is completed")
        check(all(status != "FAIL" for status in found.values()), "clean run yields no FAIL")
        rc = asd.main(["--evidence", str(evidence), "--output", str(root / "out.json")])
        check(rc == 0, "asserter exits 0 for a clean run")


def test_fabricated_or_unrestored_run_fails() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_tree(root, run_id="bad-run", manual="restriction_visible",
                   restore_proved=False, offsets_ok=False, verdict="timeout")
        evidence = root / "device-lab" / "lab" / "selfdrive"
        found = statuses(evidence)
        check(found.get("no_human_attestation") == "FAIL",
              "a fabricated restriction_visible observation is rejected")
        check(found.get("exact_restore") == "FAIL", "an unproved restore is rejected")
        check(found.get("settings_offsets") == "FAIL", "unexpected 0x44 offsets are rejected")
        check(found.get("selfdrive_verdict") == "WARN", "a non-completed verdict warns")
        rc = asd.main(["--evidence", str(evidence), "--output", str(root / "out.json")])
        check(rc == 1, "asserter exits 1 when checks fail")


def test_missing_evidence_warns() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        evidence = Path(tmp) / "empty"
        evidence.mkdir()
        found = statuses(evidence)
        check(found.get("selfdrive_present") == "WARN", "an empty evidence dir warns rather than crashes")


def main() -> int:
    test_clean_run_passes()
    test_fabricated_or_unrestored_run_fails()
    test_missing_evidence_warns()
    if FAILURES:
        print(f"{FAILURES} assert_selfdrive_evidence test(s) failed", file=sys.stderr)
        return 1
    print("PASS: assert_selfdrive_evidence advisory checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

