#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from ptc_token_v1 import TokenError, decode_token, verify_token


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_reason(reason: str, fn) -> None:
    try:
        fn()
    except TokenError as exc:
        assert_equal(exc.reason, reason, "error reason")
        return
    raise AssertionError(f"expected TokenError reason {reason!r}")


def main() -> int:
    subprocess.run([sys.executable, str(TOOLS / "make_fixtures.py")], check=True)
    fixture_path = ROOT / "tests" / "fixtures" / "token_v1_fixture.json"
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))

    device_id = fixture["device_id"]
    secret = fixture["grant_secret"]
    max_add = fixture["max_add_minutes"]
    valid = fixture["cases"][0]
    code = valid["code"]

    payload = verify_token(
        code,
        device_id=device_id,
        secret=secret,
        current_day_index=valid["day_index"],
        max_add_minutes=max_add,
        used_nonces=set(),
    )
    assert_equal(payload.minutes, 30, "payload minutes")
    assert_equal(payload.nonce, 4660, "payload nonce")

    cli = subprocess.run(
        [
            sys.executable,
            str(TOOLS / "grant_code.py"),
            "--minutes",
            "30",
            "--device",
            device_id,
            "--secret",
            secret,
            "--day-index",
            str(valid["day_index"]),
            "--nonce",
            str(valid["nonce"]),
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    assert_equal(cli.stdout.strip(), code, "CLI deterministic code")

    assert_reason("bad_signature", lambda: decode_token(code, device_id, "wrong-secret"))
    assert_reason(
        "wrong_date",
        lambda: verify_token(code, device_id, secret, valid["day_index"] + 1, max_add, set()),
    )
    assert_reason(
        "used_token",
        lambda: verify_token(code, device_id, secret, valid["day_index"], max_add, {(valid["day_index"], valid["nonce"])}),
    )

    over_limit = fixture["cases"][1]
    assert_reason(
        "minutes_exceed_limit",
        lambda: verify_token(over_limit["code"], device_id, secret, over_limit["day_index"], max_add, set()),
    )

    print("MVP token v1 tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

