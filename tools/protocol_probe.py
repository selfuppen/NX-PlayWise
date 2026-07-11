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
            "parent_unlock_until": 0,
            "last_enforced_day_index": 0,
            "last_enforced_mode": 0,
            "last_enforced_minutes": 0,
            "updated_at": 0,
        },
    )
    write_json_atomic(
        paths.app_root / "capabilities.json",
        {
            "version": 1,
            "play_timer_write_verified": False,
            "play_timer_write_backend": "pctl-s-v1",
            "raw_block_verified": False,
            "suspend_verified": False,
            "verified_at": {"play_timer_write": None, "raw_block": None, "suspend": None},
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
    request_id = write_request(Path(args.root), args.type, payload, request_id=args.request_id, created_at=args.created_at)
    print(request_id)
    return 0


def submit_raw(args: argparse.Namespace) -> int:
    root = Path(args.root)
    paths = create_layout(root)
    request_id = args.request_id
    text = args.text
    if args.file:
        text = Path(args.file).read_text(encoding="utf-8")
    if text is None:
        raise SystemExit("--text or --file is required")
    write_json_atomic(paths.pending / f"{request_id}.json", {})
    pending = paths.pending / f"{request_id}.json"
    tmp = pending.with_name(pending.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(pending)
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
    request.add_argument("--request-id")
    request.add_argument("--created-at", type=int)
    request.set_defaults(func=submit_request)

    raw = sub.add_parser("raw-request", help="Write raw text to a pending request file.")
    raw.add_argument("--root", required=True)
    raw.add_argument("--request-id", required=True)
    raw.add_argument("--text")
    raw.add_argument("--file")
    raw.set_defaults(func=submit_raw)

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

