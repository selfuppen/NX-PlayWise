#!/usr/bin/env python3
from __future__ import annotations

import argparse
import secrets

from ptc_token_v1 import (
    MAX_NONCE,
    TOKEN_ACTION_ADD_TODAY_MINUTES,
    TOKEN_VERSION,
    TokenPayload,
    encode_token,
)


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a v1 offline play-time grant code.")
    parser.add_argument("--minutes", type=int, required=True, help="Minutes to add today.")
    parser.add_argument("--device", required=True, help="Device ID configured on the Switch.")
    parser.add_argument("--secret", required=True, help="Grant secret configured on the Switch.")
    parser.add_argument("--day-index", type=parse_int, required=True, help="Day index since 2020-01-01.")
    parser.add_argument("--nonce", type=parse_int, help="25-bit nonce. Defaults to random.")
    args = parser.parse_args()

    nonce = args.nonce if args.nonce is not None else secrets.randbelow(MAX_NONCE + 1)
    payload = TokenPayload(
        version=TOKEN_VERSION,
        action=TOKEN_ACTION_ADD_TODAY_MINUTES,
        minutes=args.minutes,
        day_index_since_2020=args.day_index,
        nonce=nonce,
    )
    print(encode_token(payload, args.device, args.secret))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

