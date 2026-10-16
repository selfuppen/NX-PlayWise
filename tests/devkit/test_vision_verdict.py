#!/usr/bin/env python3
"""Host regression for tools/automation/vision_verdict.py (no real device).

Verifies that sealing binds the AI verdict to the exact screenshot bytes the
driver captured: a changed or missing frame, a bad enum, or a single-frame
bundle must refuse to seal, and an honest negative must not satisfy the
qualification matrix.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import tools.automation.vision_verdict as vv  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


def _build_bundle(reports: Path, run_id: str = "pause-a", frames: int = 3,
                  sysbotbase_commit: str = "ae13548", with_wire: bool = True) -> Path:
    run_dir = reports / "vision" / run_id
    run_dir.mkdir(parents=True)
    shots = []
    for index in range(frames):
        shot = run_dir / f"{index:02d}.jpg"
        shot.write_bytes(b"\xff\xd8" + f"frame-{index}".encode("ascii"))
        shots.append({"checkpoint": f"f{index}", "path": shot.relative_to(reports).as_posix(),
                      "sha256": vv.sha256_file(shot), "captured_at": "t"})
    wire_log = run_dir / "wire.log"
    wire_log.write_text("00:00\tpixelPeek baseline\n", encoding="utf-8")
    pending = {
        "schema_version": 1,
        "run_id": run_id,
        "campaign_id": "campaign-test",
        "slot": "pause_on_game_a",
        "sysbotbase_commit": sysbotbase_commit,
        "wire_log": wire_log.relative_to(reports).as_posix(),
        "wire_log_sha256": vv.sha256_file(wire_log) if with_wire else "",
        "screenshots": shots,
    }
    pending_path = run_dir / "pending.json"
    pending_path.write_text(json.dumps(pending), encoding="utf-8")
    return pending_path


def _expect_refusal(label: str, func) -> None:
    try:
        func()
    except vv.VerdictError:
        return
    check(False, label)


def test_seal_success() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        pending = _build_bundle(reports)
        verdict = vv.seal(reports, pending, observation="restriction_visible",
                          runtime_effect="paused_or_suspended", context_confirmed=True,
                          confidence="high", reasoning="frame f1 shows the system play-time prompt")
        out = vv.write_verdict(reports, verdict)
        check(out == reports / "pause-a.vision.json", "sidecar is written next to the report")
        data = json.loads(out.read_text(encoding="utf-8"))
        check(data["method"] == "vision_ai", "sealed method is vision_ai")
        check(data["observation"] == "restriction_visible", "observation recorded verbatim")
        check(data["sysbotbase_commit"] == "ae13548", "sys-botbase commit carried from pending")
        check(len(data["screenshots"]) == 3, "every screenshot is bound")
        check(vv.verdict_satisfies_matrix(data), "restriction_visible + paused satisfies the matrix")


def test_seal_rejects_changed_screenshot() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        pending = _build_bundle(reports)
        (reports / "vision" / "pause-a" / "00.jpg").write_bytes(b"\xff\xd8tampered")
        _expect_refusal("a changed screenshot must refuse to seal",
                        lambda: vv.seal(reports, pending, observation="restriction_visible",
                                        runtime_effect="paused_or_suspended", context_confirmed=True,
                                        confidence="high", reasoning="x"))


def test_seal_rejects_missing_screenshot() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        pending = _build_bundle(reports)
        (reports / "vision" / "pause-a" / "01.jpg").unlink()
        _expect_refusal("a missing screenshot must refuse to seal",
                        lambda: vv.seal(reports, pending, observation="restriction_visible",
                                        runtime_effect="paused_or_suspended", context_confirmed=True,
                                        confidence="high", reasoning="x"))


def test_seal_rejects_bad_enum() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        pending = _build_bundle(reports)
        _expect_refusal("an invalid observation enum must refuse to seal",
                        lambda: vv.seal(reports, pending, observation="definitely_restricted",
                                        runtime_effect="paused_or_suspended", context_confirmed=True,
                                        confidence="high", reasoning="x"))


def test_seal_requires_two_frames() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        pending = _build_bundle(reports, frames=1)
        _expect_refusal("a single-frame bundle must refuse to seal",
                        lambda: vv.seal(reports, pending, observation="restriction_visible",
                                        runtime_effect="paused_or_suspended", context_confirmed=True,
                                        confidence="high", reasoning="x"))


def test_honest_negative_does_not_satisfy_matrix() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        pending = _build_bundle(reports)
        verdict = vv.seal(reports, pending, observation="no_visible_restriction",
                          runtime_effect="continued", context_confirmed=True,
                          confidence="low", reasoning="no prompt in any captured frame")
        check(not vv.verdict_satisfies_matrix(verdict),
              "an honest no_visible_restriction verdict must not satisfy the matrix")


def main() -> int:
    test_seal_success()
    test_seal_rejects_changed_screenshot()
    test_seal_rejects_missing_screenshot()
    test_seal_rejects_bad_enum()
    test_seal_requires_two_frames()
    test_honest_negative_does_not_satisfy_matrix()
    if FAILURES:
        print(f"{FAILURES} vision_verdict test(s) failed", file=sys.stderr)
        return 1
    print("PASS: vision_verdict sealing binds verdicts to retained screenshots")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
