#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from ptc_observe_processor import ObserveProcessor
from ptc_request_queue import AppPaths, read_json, write_request
from ptc_token_v1 import TOKEN_ACTION_ADD_TODAY_MINUTES, TOKEN_VERSION, TokenPayload, encode_token


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_true(value, label: str) -> None:
    if not value:
        raise AssertionError(f"{label}: expected truthy value")


def make_code(minutes: int, day_index: int, nonce: int, secret: str = "test-secret") -> str:
    return encode_token(
        TokenPayload(
            version=TOKEN_VERSION,
            action=TOKEN_ACTION_ADD_TODAY_MINUTES,
            minutes=minutes,
            day_index_since_2020=day_index,
            nonce=nonce,
        ),
        "test-device",
        secret,
    )


def result_for(paths: AppPaths, request_id: str) -> dict:
    return read_json(paths.results / f"{request_id}.json")


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="ptc-observe-"))
    try:
        subprocess.run(
            [
                sys.executable,
                str(TOOLS / "protocol_probe.py"),
                "init",
                "--root",
                str(tmp),
                "--device",
                "test-device",
                "--secret",
                "test-secret",
            ],
            check=True,
            text=True,
            capture_output=True,
        )
        paths = AppPaths(tmp)

        status_id = write_request(tmp, "status", request_id="1000-0001", created_at=1000)
        ObserveProcessor(tmp, current_day_index=2380).process_all()
        status_result = result_for(paths, status_id)
        assert_equal(status_result["status"], "ok", "status result")
        assert_equal(status_result["mode"], "observe", "status mode")
        assert_equal(status_result["dry_run"], True, "status dry_run")
        assert_true((paths.done / f"{status_id}.json").exists(), "status archived")

        code = make_code(30, 2380, 4660)
        grant_id = write_request(tmp, "offline_code", {"code": code}, request_id="1000-0002", created_at=1001)
        ObserveProcessor(tmp, current_day_index=2380).process_all()
        grant_result = result_for(paths, grant_id)
        assert_equal(grant_result["status"], "ok", "grant result")
        assert_equal(grant_result["dry_run"], True, "grant dry_run")
        assert_equal(grant_result["applied"]["minutes"], 30, "grant minutes")
        assert_equal(grant_result["applied"]["pending_apply"], True, "pending apply")
        assert_true(not (paths.ledger / "used_nonces.jsonl").exists(), "observe does not consume nonce")

        bad_code = make_code(30, 2380, 4661, secret="wrong-secret")
        bad_id = write_request(tmp, "offline_code", {"code": bad_code}, request_id="1000-0003", created_at=1002)
        ObserveProcessor(tmp, current_day_index=2380).process_all()
        bad_result = result_for(paths, bad_id)
        assert_equal(bad_result["status"], "error", "bad result")
        assert_equal(bad_result["error"]["reason"], "bad_signature", "bad reason")
        assert_equal(bad_result["dry_run"], True, "bad dry_run")

        over_code = make_code(180, 2380, 4662)
        over_id = write_request(tmp, "offline_code", {"code": over_code}, request_id="1000-0004", created_at=1003)
        ObserveProcessor(tmp, current_day_index=2380).process_all()
        over_result = result_for(paths, over_id)
        assert_equal(over_result["status"], "error", "over result")
        assert_equal(over_result["error"]["reason"], "minutes_exceed_limit", "over reason")

        manual_id = write_request(tmp, "status", request_id="1000-0005", created_at=1004)
        manual = subprocess.run(
            [
                sys.executable,
                str(TOOLS / "make_result.py"),
                "--root",
                str(tmp),
                "--request-id",
                manual_id,
                "--day-index",
                "2380",
                "--completed-at",
                "1005",
            ],
            check=True,
            text=True,
            capture_output=True,
        )
        manual_path = Path(manual.stdout.strip())
        assert_equal(manual_path.name, f"{manual_id}.json", "manual result file")
        assert_equal(manual_path.parent.name, "results", "manual result directory")
        manual_result = result_for(paths, manual_id)
        assert_equal(manual_result["request_id"], manual_id, "manual result id")
        assert_equal(manual_result["status"], "ok", "manual result status")
        assert_equal(manual_result["state"]["day_index"], 2380, "manual result day")

        print("Observe queue tests passed")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())

