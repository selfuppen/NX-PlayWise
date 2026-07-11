#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import date, datetime, timedelta, timezone
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


def parse_date(value: str) -> date:
    try:
        return datetime.strptime(value, "%Y-%m-%d").date()
    except ValueError as exc:
        raise argparse.ArgumentTypeError("date must use YYYY-MM-DD") from exc


def day_index_for(target: date) -> int:
    return (target - date(2020, 1, 1)).days


def today_utc8() -> date:
    return datetime.now(timezone(timedelta(hours=8))).date()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a v1 offline play-time grant code.")
    parser.add_argument("--minutes", type=int, required=True, help="Minutes to add today.")
    parser.add_argument("--device", required=True, help="Device ID configured on the Switch.")
    parser.add_argument("--secret", required=True, help="Grant secret configured on the Switch.")
    day = parser.add_mutually_exclusive_group()
    day.add_argument("--day-index", type=parse_int, help="Day index since 2020-01-01.")
    day.add_argument("--date", type=parse_date, help="Local date in YYYY-MM-DD form. Defaults to today when omitted.")
    parser.add_argument("--nonce", type=parse_int, help="25-bit nonce. Defaults to random.")
    args = parser.parse_args()

    day_index = args.day_index if args.day_index is not None else day_index_for(args.date or today_utc8())
    nonce = args.nonce if args.nonce is not None else secrets.randbelow(MAX_NONCE + 1)
    payload = TokenPayload(
        version=TOKEN_VERSION,
        action=TOKEN_ACTION_ADD_TODAY_MINUTES,
        minutes=args.minutes,
        day_index_since_2020=day_index,
        nonce=nonce,
    )
    print(encode_token(payload, args.device, args.secret))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

