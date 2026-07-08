#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path

from ptc_token_v1 import (
    TOKEN_ACTION_ADD_TODAY_MINUTES,
    TOKEN_VERSION,
    TokenPayload,
    encode_token,
)


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "fixtures" / "token_v1_fixture.json"


def main() -> int:
    device_id = "test-device"
    secret = "test-secret"
    cases = [
        {
            "name": "valid_30min",
            "minutes": 30,
            "day_index": 2380,
            "nonce": 4660,
            "expect": "ok",
        },
        {
            "name": "over_limit_180min",
            "minutes": 180,
            "day_index": 2380,
            "nonce": 4661,
            "expect": "minutes_exceed_limit",
        },
    ]
    for case in cases:
        payload = TokenPayload(
            version=TOKEN_VERSION,
            action=TOKEN_ACTION_ADD_TODAY_MINUTES,
            minutes=case["minutes"],
            day_index_since_2020=case["day_index"],
            nonce=case["nonce"],
        )
        case["code"] = encode_token(payload, device_id, secret)

    fixture = {
        "version": 1,
        "device_id": device_id,
        "grant_secret": secret,
        "max_add_minutes": 120,
        "cases": cases,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(fixture, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

