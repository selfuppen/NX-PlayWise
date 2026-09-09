#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def template(commit: str, release_id: str, model: str, hos: str, atmosphere: str) -> dict:
    recovery = {
        action: {"request_submitted": None, "pctl_reread": None, "popup_cleared": None}
        for action in ("skip_instance", "disable_bedtime", "restore_install_snapshot")
    }
    lifecycle = {
        phase: None
        for phase in ("home", "foreground", "suspend", "sleep", "reboot", "cross_day",
                      "manual_clock_change", "recovery_failure")
    }
    return {
        "schema_version": 1,
        "report_status": "draft",
        "subject": {"commit": commit, "release_id": release_id},
        "environment": {"model": model, "hos": hos, "atmosphere": atmosphere},
        "games": ["", ""],
        "timing": {"boundary_trigger_seconds": None, "wake_trigger_seconds": None},
        "blocked_entries": {
            "game": None,
            "homebrew": None,
            "home": None,
            "system_settings": None,
            "playwise_nro": None,
        },
        "overlay": {
            "opened_during_popup": None,
            "pin_verified": None,
            "shared_cooldown_verified": None,
            "single_action_authorization_verified": None,
            "daily_limit_message_verified": None,
            "recovery": recovery,
            "backend_failure": {
                "reported_unconfirmed": None,
                "external_recovery_shown": None,
                "startup_restore_flag_created": None,
            },
        },
        "official_pause": {
            "on_recorded": None,
            "off_recorded": None,
            "playwise_modified_setting": None,
        },
        "lifecycle": lifecycle,
        "pctl": {
            "settings_exactly_restored": None,
            "unexpected_raw_offsets": None,
            "bedtime_consumed_daily_allowance": None,
        },
        "missing_overlay": {
            "warning_unskippable": None,
            "pin_and_long_hold_required": None,
            "enable_allowed_after_confirmation": None,
            "schedule_continues_without_handshake": None,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 bedtime 真机资格报告草稿。")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--release-id", required=True)
    parser.add_argument("--model", default="mariko-oled")
    parser.add_argument("--hos", default="22.5.0")
    parser.add_argument("--atmosphere", default="1.11.2")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(template(args.commit, args.release_id, args.model, args.hos, args.atmosphere),
                   ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
