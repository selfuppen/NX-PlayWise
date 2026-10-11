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
import make_eye_care_qualification_template as make_eye_care  # noqa: E402


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


def _eye_care_pass_answers() -> list[str]:
    return (
        ["Eye A", "Eye B", "30", "20"]
        + ["y"] * len(make_eye_care.OBSERVATION_FIELDS)   # observations
        + ["y", "n"]                                      # 0x44 restored; did not consume allowance
    )


def test_eye_care_capture_is_accepted_by_gate() -> None:
    with tempfile.TemporaryDirectory(prefix="cap-eye-") as tmp:
        reports = Path(tmp) / "device-reports"
        argv = ["eye-care", "--commit", COMMIT, "--release-id", RELEASE_ID,
                "--model", "mariko-oled", "--reports", str(reports)]
        rc = cap.main(argv, reader=make_reader(_eye_care_pass_answers()))
        require(rc == 0, "eye-care capture must succeed")
        out = reports / "eye-care-qualification.json"
        require(out.is_file(), "eye-care-qualification.json must be written")
        manifest = {"commit": COMMIT, "release_id": RELEASE_ID}
        # Must satisfy the authoritative gate's eye-care verifier.
        vq.verify_eye_care_report(reports, manifest, "oled", "22.5.0", "1.11.2", required=True)


def test_eye_care_consumed_allowance_is_rejected_by_gate() -> None:
    with tempfile.TemporaryDirectory(prefix="cap-eye-bad-") as tmp:
        reports = Path(tmp) / "device-reports"
        argv = ["eye-care", "--commit", COMMIT, "--release-id", RELEASE_ID, "--reports", str(reports)]
        # Flip only the final answer: the rest DID consume the daily allowance.
        answers = (["Eye A", "Eye B", "30", "20"]
                   + ["y"] * len(make_eye_care.OBSERVATION_FIELDS) + ["y", "y"])
        rc = cap.main(argv, reader=make_reader(answers))
        require(rc == 0, "capture itself still records the (bad) observation")
        manifest = {"commit": COMMIT, "release_id": RELEASE_ID}
        try:
            vq.verify_eye_care_report(reports, manifest, "oled", "22.5.0", "1.11.2", required=True)
        except vq.QualificationError as exc:
            require("额度隔离" in str(exc), "the gate must reject a rest that consumed the daily allowance")
        else:
            raise AssertionError("an allowance-consuming eye-care report must not pass the gate")


def main() -> int:
    test_bedtime_capture_is_accepted_by_gate()
    test_invalid_int_is_rejected()
    test_eye_care_capture_is_accepted_by_gate()
    test_eye_care_consumed_allowance_is_rejected_by_gate()
    print("Capture observations tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
