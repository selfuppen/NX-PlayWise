#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil


APP_DIR = Path("switch") / "play-time-control"


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def create_package(out: Path, mode: str, include_boot2: bool) -> None:
    if out.exists():
        shutil.rmtree(out)
    app = out / APP_DIR
    for directory in [
        app / "inbox" / "pending",
        app / "inbox" / "processing",
        app / "inbox" / "done",
        app / "results",
        app / "logs",
        app / "ledger",
        app / "backups",
        app / "flags",
    ]:
        directory.mkdir(parents=True, exist_ok=True)

    control_mode = "observe" if mode == "safe" else mode
    write_json(
        app / "config.json",
        {
            "version": 1,
            "device_id": "kid-switch",
            "grant_secret": "replace-with-long-random-secret",
            "max_add_minutes": 120,
            "control_mode": control_mode,
            "allow_unlimited_to_limited": False,
            "default_request_timeout_ms": 8000,
        },
    )
    write_json(app / "auth.json", {"version": 1, "pin_hash": "", "pin_salt": "", "hash": "hmac-sha256", "updated_at": 0})
    write_json(
        app / "rules.json",
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
    write_json(app / "state.json", {"version": 1, "day_index": None, "parent_unlock": {"active": False, "until": 0}, "bedtime_active": False, "last_applied": None})
    write_json(app / "capabilities.json", {"version": 1, "raw_block_verified": False, "suspend_verified": False, "verified_at": {"raw_block": None, "suspend": None}})

    if include_boot2:
        boot2 = out / "atmosphere" / "contents" / "010000000000BD23" / "flags" / "boot2.flag"
        boot2.parent.mkdir(parents=True, exist_ok=True)
        boot2.write_text("", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create staged SDMC packages for play-time-control.")
    parser.add_argument("--mode", choices=["safe", "observe", "disabled", "grant", "enforce"], default="safe")
    parser.add_argument("--out", required=True)
    parser.add_argument("--boot2", action="store_true", help="Explicitly include Atmosphere boot2 flag.")
    args = parser.parse_args()

    create_package(Path(args.out), args.mode, args.boot2)
    print(Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
