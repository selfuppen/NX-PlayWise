#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from ptc_token_v2 import MAX_VALUE, TokenError, decode_token, encode_token, tier_for_minutes, verify_token


def assert_reason(reason: str, fn) -> None:
    try:
        fn()
    except TokenError as exc:
        if exc.reason != reason:
            raise AssertionError(f"expected {reason!r}, got {exc.reason!r}") from exc
        return
    raise AssertionError(f"expected TokenError reason {reason!r}")


def main() -> int:
    device = "test-device"
    secret = "test-secret"
    day_index = 2380

    for tier_index in range(24):
        minutes = (tier_index + 1) * 5
        code = encode_token(tier_index, tier_index, device, secret, day_index)
        if len(code) != 8 or not code.isascii() or not code.isdigit():
            raise AssertionError(f"tier {tier_index} did not produce eight ASCII digits: {code!r}")
        payload = verify_token(code, device, secret, day_index, 120, set())
        if payload.tier_index != tier_index or payload.minutes != minutes or payload.nonce != tier_index:
            raise AssertionError(f"tier {tier_index} round trip mismatch: {payload!r}")
        if tier_for_minutes(minutes) != tier_index:
            raise AssertionError(f"minute mapping mismatch for tier {tier_index}")

    leading_zero = encode_token(0, 0, device, secret, day_index)
    if not leading_zero.startswith("0"):
        raise AssertionError(f"expected leading-zero token, got {leading_zero}")
    assert_reason("bad_signature", lambda: decode_token(leading_zero, "other-device", secret, day_index))
    assert_reason("bad_signature", lambda: decode_token(leading_zero, device, "wrong-secret", day_index))
    assert_reason("bad_signature", lambda: decode_token(leading_zero, device, secret, day_index + 1))
    assert_reason("bad_code", lambda: decode_token("1234567", device, secret, day_index))
    assert_reason("bad_code", lambda: decode_token("1234567A", device, secret, day_index))
    assert_reason("bad_code", lambda: decode_token(f"{MAX_VALUE + 1:08d}", device, secret, day_index))
    assert_reason("bad_code", lambda: decode_token(f"{24 << 21:08d}", device, secret, day_index))
    assert_reason("minutes_exceed_limit", lambda: verify_token(
        encode_token(23, 1, device, secret, day_index), device, secret, day_index, 60, set()))
    assert_reason("used_token", lambda: verify_token(
        leading_zero, device, secret, day_index, 120, {(day_index, 0)}))

    with tempfile.TemporaryDirectory(prefix="ptc-v2-nonce-") as tmp:
        state_path = Path(tmp) / "nonces.json"
        base = [
            sys.executable, str(TOOLS / "grant_code.py"), "--tier-minutes", "30",
            "--device", device, "--secret", secret, "--day-index", str(day_index),
            "--v2-nonce-state", str(state_path),
        ]
        first = subprocess.run(base, check=True, text=True, capture_output=True).stdout.strip()
        second = subprocess.run(base, check=True, text=True, capture_output=True).stdout.strip()
        if decode_token(first, device, secret, day_index).nonce != 0:
            raise AssertionError("first generated v2 nonce is not zero")
        if decode_token(second, device, secret, day_index).nonce != 1:
            raise AssertionError("second generated v2 nonce did not increment")
        state_path.write_text(json.dumps({f"{device}\0{day_index}": 512}), encoding="utf-8")
        exhausted = subprocess.run(base, text=True, capture_output=True)
        if exhausted.returncode == 0 or "quota is exhausted" not in exhausted.stderr:
            raise AssertionError("v2 daily nonce exhaustion was not reported")

    print("MVP token v2 tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
