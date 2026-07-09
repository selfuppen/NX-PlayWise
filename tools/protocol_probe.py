#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

from ptc_observe_processor import ObserveProcessor
from ptc_request_queue import create_layout, write_json_atomic, write_request


def init_sdmc(args: argparse.Namespace) -> int:
    root = Path(args.root)
    paths = create_layout(root)
    write_json_atomic(
        paths.app_root / "config.json",
        {
            "version": 1,
            "device_id": args.device,
            "grant_secret": args.secret,
            "max_add_minutes": args.max_add_minutes,
            "control_mode": "observe",
            "allow_unlimited_to_limited": False,
            "default_request_timeout_ms": 60000,
        },
    )
    write_json_atomic(
        paths.app_root / "auth.json",
        {
            "version": 1,
            "pin_hash": "",
            "pin_salt": "",
            "hash": "hmac-sha256",
            "updated_at": 0,
        },
    )
    write_json_atomic(
        paths.app_root / "rules.json",
        {
            "version": 1,
            "week": [
                {"mode": "unlimited", "minutes": 120},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "limit", "minutes": 60},
                {"mode": "unlimited", "minutes": 120},
            ],
            "today_override": None,
            "bedtime": {"enabled": False, "start_min": 1260, "end_min": 480},
            "limit_action": "remind",
        },
    )
    write_json_atomic(
        paths.app_root / "state.json",
        {
            "version": 1,
            "day_index": None,
            "parent_unlock": {"active": False, "until": 0},
            "bedtime_active": False,
            "last_applied": None,
        },
    )
    write_json_atomic(
        paths.app_root / "capabilities.json",
        {
            "version": 1,
            "raw_block_verified": False,
            "suspend_verified": False,
            "verified_at": {"raw_block": None, "suspend": None},
        },
    )
    print(paths.app_root)
    return 0


def submit_request(args: argparse.Namespace) -> int:
    payload = {}
    if args.type == "offline_code":
        if not args.code:
            raise SystemExit("--code is required for offline_code")
        payload["code"] = args.code
    if args.payload_json:
        payload.update(json.loads(args.payload_json))
    request_id = write_request(Path(args.root), args.type, payload)
    print(request_id)
    return 0


def process_observe(args: argparse.Namespace) -> int:
    count = ObserveProcessor(Path(args.root), args.day_index).process_all()
    print(f"processed={count}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Probe the v1 file protocol on a host SDMC-like directory.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    init = sub.add_parser("init", help="Create a safe observe SDMC layout.")
    init.add_argument("--root", required=True)
    init.add_argument("--device", required=True)
    init.add_argument("--secret", required=True)
    init.add_argument("--max-add-minutes", type=int, default=120)
    init.set_defaults(func=init_sdmc)

    request = sub.add_parser("request", help="Write a pending request.")
    request.add_argument("--root", required=True)
    request.add_argument("--type", required=True)
    request.add_argument("--code")
    request.add_argument("--payload-json")
    request.set_defaults(func=submit_request)

    process = sub.add_parser("process-observe", help="Process pending requests in observe dry-run mode.")
    process.add_argument("--root", required=True)
    process.add_argument("--day-index", required=True, type=int)
    process.set_defaults(func=process_observe)

    return parser


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())

