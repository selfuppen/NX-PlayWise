#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import capture_device_observations as cap  # noqa: E402
import verify_device_qualification as vq  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def make_reader(answers: list[str]):
    pending = iter(answers)

    def reader(_prompt: str) -> str:
        return next(pending)

    return reader


COMMIT = "a" * 40
RELEASE_ID = "playwise-9.9.9+aaaaaaaaaaaa"


def _bedtime_pass_answers() -> list[str]:
    return (
        ["Game A", "Game B", "5", "7"]
        + ["y"] * 5          # blocked_entries
        + ["y"] * 5          # overlay flags
        + ["y"] * 9          # 3 recovery actions x 3
        + ["y", "y", "n"]    # backend_failure (startup flag must be false)
        + ["y", "y", "n"]    # official_pause (playwise did not modify)
        + ["y"] * 8          # lifecycle
        + ["y", "n"]         # pctl restored; did not consume allowance
        + ["y"] * 4          # missing_overlay
    )

def test_bedtime_capture_is_accepted_by_gate() -> None:
    with tempfile.TemporaryDirectory(prefix="cap-bed-") as tmp:
        reports = Path(tmp) / "device-reports"
        argv = ["bedtime", "--commit", COMMIT, "--release-id", RELEASE_ID,
                "--model", "mariko-oled", "--reports", str(reports)]
        rc = cap.main(argv, reader=make_reader(_bedtime_pass_answers()))
        require(rc == 0, "bedtime capture must succeed")
        out = reports / "bedtime-qualification.json"
        require(out.is_file(), "bedtime-qualification.json must be written")
        manifest = {"commit": COMMIT, "release_id": RELEASE_ID}
        # Must satisfy the authoritative gate's bedtime verifier.
        vq.verify_bedtime_report(reports, manifest, "oled", "22.5.0", "1.11.2")


def test_invalid_int_is_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="cap-bad-") as tmp:
        reports = Path(tmp) / "device-reports"
        argv = ["bedtime", "--commit", COMMIT, "--release-id", RELEASE_ID, "--reports", str(reports)]
        # Two game names, then three invalid boundary-seconds answers exhaust the retries.
        rc = cap.main(argv, reader=make_reader(["Game A", "Game B", "x", "y", "z"]))
        require(rc == 1, "an invalid numeric answer must make capture exit 1")
        require(not (reports / "bedtime-qualification.json").is_file(),
                "no report must be written when input is rejected")


def test_eye_care_is_advisory() -> None:
    with tempfile.TemporaryDirectory(prefix="cap-eye-") as tmp:
        reports = Path(tmp) / "device-reports"
        argv = ["eye-care", "--commit", COMMIT, "--release-id", RELEASE_ID, "--reports", str(reports)]
        answers = ["Eye A", "Eye B"] + ["y"] * len(cap.EYE_CARE_FIELDS)
        rc = cap.main(argv, reader=make_reader(answers))
        require(rc == 0, "eye-care capture must succeed")
        import json
        report = json.loads((reports / "eye-care-observations.json").read_text(encoding="utf-8"))
        require(report.get("advisory") is True, "eye-care schema must be flagged advisory")
        require(report.get("report_status") == "final", "eye-care report must be final")
        require(report["observations"]["sleep_excludes_usage"] is True, "sleep-not-counted must be captured")


def main() -> int:
    test_bedtime_capture_is_accepted_by_gate()
    test_invalid_int_is_rejected()
    test_eye_care_is_advisory()
    print("Capture observations tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
