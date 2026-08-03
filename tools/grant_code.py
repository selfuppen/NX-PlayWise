#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import date, datetime, timedelta, timezone
import json
from pathlib import Path
import secrets

from ptc_token_v1 import (
    MAX_NONCE,
    TOKEN_ACTION_ADD_TODAY_MINUTES,
    TOKEN_VERSION,
    TokenPayload,
    encode_token,
)
from ptc_token_v2 import MAX_NONCE as V2_MAX_NONCE, encode_token as encode_token_v2, tier_for_minutes


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


def default_v2_nonce_state() -> Path:
    return Path.home() / ".ptc" / "token_v2_nonce_state.json"


def next_v2_nonce(path: Path, device: str, day_index: int) -> int:
    state: dict[str, int] = {}
    if path.exists():
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(loaded, dict):
                raise ValueError("state root is not an object")
            state = {str(key): int(value) for key, value in loaded.items()}
        except (OSError, ValueError, TypeError) as exc:
            raise ValueError(f"cannot read v2 nonce state {path}: {exc}") from exc
    key = f"{device}\0{day_index}"
    nonce = state.get(key, 0)
    if nonce < 0 or nonce > V2_MAX_NONCE:
        raise ValueError("today's v2 short-code quota is exhausted (512 codes)")
    state[key] = nonce + 1
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(state, sort_keys=True), encoding="utf-8")
    temporary.replace(path)
    return nonce


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a v1 or v2 offline play-time grant code.")
    amount = parser.add_mutually_exclusive_group(required=True)
    amount.add_argument("--minutes", type=int, help="Minutes to add today (v1, arbitrary 1-1440).")
    amount.add_argument("--tier-minutes", type=int, help="v2 tier: a multiple of 5 from 5 through 120.")
    parser.add_argument("--device", required=True, help="Device ID configured on the Switch.")
    parser.add_argument("--secret", required=True, help="Grant secret configured on the Switch.")
    day = parser.add_mutually_exclusive_group()
    day.add_argument("--day-index", type=parse_int, help="Day index since 2020-01-01.")
    day.add_argument("--date", type=parse_date, help="Local date in YYYY-MM-DD form. Defaults to today when omitted.")
    parser.add_argument("--nonce", type=parse_int, help="Nonce override (v1: 25-bit, v2: 9-bit).")
    parser.add_argument("--v2-nonce-state", type=Path, default=default_v2_nonce_state(), help="Local v2 nonce state file.")
    args = parser.parse_args()

    day_index = args.day_index if args.day_index is not None else day_index_for(args.date or today_utc8())
    if args.tier_minutes is not None:
        try:
            tier_index = tier_for_minutes(args.tier_minutes)
            nonce = args.nonce if args.nonce is not None else next_v2_nonce(args.v2_nonce_state, args.device, day_index)
            if not 0 <= nonce <= V2_MAX_NONCE:
                raise ValueError("v2 nonce must be in range 0..511")
            code = encode_token_v2(tier_index, nonce, args.device, args.secret, day_index)
        except ValueError as exc:
            parser.error(str(exc))
        print(code)
    else:
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

