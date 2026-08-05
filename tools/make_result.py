#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Any

from ptc_request_queue import AppPaths, read_json, write_json_atomic


ERRORS: dict[str, tuple[int, str]] = {
    "unsupported_version": (100, "不支持的协议版本"),
    "bad_request": (101, "请求格式无效"),
    "unknown_request_type": (102, "未知请求类型"),
    "request_expired": (103, "请求已过期"),
    "bad_code": (200, "授权码格式无效"),
    "bad_token_version": (201, "授权码版本不支持"),
    "unsupported_action": (202, "授权码动作不支持"),
    "bad_signature": (203, "授权码签名不匹配"),
    "bad_clock": (204, "系统时间不可用"),
    "wrong_date": (205, "授权码不是今天有效"),
    "used_token": (206, "授权码已经使用过"),
    "minutes_exceed_limit": (207, "授权分钟数超过上限"),
    "code_cooldown": (208, "短码错误次数过多，请稍后再试"),
    "disabled": (300, "后台当前已禁用"),
    "unlimited_not_allowed": (301, "当前无限制状态不允许改为有限制"),
    "raw_block_not_verified": (302, "禁玩能力尚未验证"),
    "suspend_not_verified": (303, "暂停能力尚未验证"),
    "pctl_init_failed": (400, "家长控制服务初始化失败"),
    "pctl_read_failed": (401, "读取家长控制状态失败"),
    "pctl_write_failed": (402, "写入家长控制设置失败"),
    "pctl_backup_failed": (403, "备份家长控制设置失败"),
    "storage_read_failed": (500, "读取存储文件失败"),
    "storage_write_failed": (501, "写入存储文件失败"),
    "config_invalid": (502, "配置文件无效"),
    "rules_invalid": (503, "规则文件无效"),
}


def parse_int(value: str) -> int:
    return int(value, 0)


def latest_request(paths: AppPaths) -> Path:
    files = sorted(paths.pending.glob("*.json"), key=lambda path: path.stat().st_mtime)
    if not files:
        raise SystemExit(f"no pending request JSON found under {paths.pending}")
    return files[-1]


def request_from_args(args: argparse.Namespace, paths: AppPaths) -> dict[str, Any]:
    if args.request:
        return read_json(Path(args.request))
    if args.request_id:
        path = paths.pending / f"{args.request_id}.json"
        if path.exists():
            return read_json(path)
        return {
            "version": 1,
            "request_id": args.request_id,
            "type": args.type,
            "payload": {},
        }
    return read_json(latest_request(paths))


def capabilities(paths: AppPaths) -> dict[str, bool]:
    path = paths.app_root / "capabilities.json"
    if path.exists():
        data = read_json(path)
        return {
            "raw_block_verified": bool(data.get("raw_block_verified", False)),
            "suspend_verified": bool(data.get("suspend_verified", False)),
        }
    return {"raw_block_verified": False, "suspend_verified": False}


def state(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "day_index": args.day_index,
        "limited_today": args.limited_today,
        "blocked_today": args.blocked_today,
        "unrestricted_today": args.unrestricted_today,
        "remaining_available": args.remaining_minutes >= 0,
        "remaining_minutes": args.remaining_minutes,
        "play_timer_enabled": args.play_timer_enabled,
        "restricted_now": args.restricted_now,
        "bedtime_active": args.bedtime_active,
        "parent_unlock_active": args.parent_unlock_active,
    }


def result_for(args: argparse.Namespace, request: dict[str, Any], caps: dict[str, bool]) -> dict[str, Any]:
    request_id = str(request.get("request_id") or "")
    request_type = str(request.get("type") or args.type)
    result = {
        "version": 1,
        "request_id": request_id,
        "type": request_type,
        "status": args.status,
        "mode": args.mode,
        "dry_run": args.dry_run,
        "state": state(args),
        "capabilities": caps,
        "completed_at": args.completed_at if args.completed_at is not None else int(time.time()),
    }
    if args.status == "error":
        code, message = ERRORS[args.reason]
        result["error"] = {"code": code, "reason": args.reason, "message": message}
    if args.status == "ok" and request_type == "offline_code" and args.minutes is not None:
        result["applied"] = {
            "minutes": args.minutes,
            "today_limit": None,
            "pending_apply": True,
        }
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Write a matching v1 result JSON for manual companion testing.")
    parser.add_argument("--root", required=True, help="SD card root or host SDMC-like root containing switch/playwise.")
    parser.add_argument("--request", help="Path to a request JSON file. Defaults to the newest pending request.")
    parser.add_argument("--request-id", help="Request id to read from inbox/pending or synthesize.")
    parser.add_argument("--type", default="status", help="Request type when --request-id has no pending file.")
    parser.add_argument("--status", choices=["ok", "error"], default="ok")
    parser.add_argument("--reason", choices=sorted(ERRORS), default="disabled", help="Error reason when --status error.")
    parser.add_argument("--mode", default="observe", choices=["observe", "grant", "enforce", "disabled"])
    parser.add_argument("--dry-run", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--day-index", type=parse_int, default=2380)
    parser.add_argument("--completed-at", type=parse_int)
    parser.add_argument("--minutes", type=parse_int, help="Include an offline_code applied.minutes block for ok results.")
    parser.add_argument("--remaining-minutes", type=parse_int, default=-1)
    parser.add_argument("--limited-today", type=parse_int, default=-1)
    parser.add_argument("--blocked-today", type=parse_int, default=-1)
    parser.add_argument("--unrestricted-today", type=parse_int, default=-1)
    parser.add_argument("--play-timer-enabled", type=parse_int, default=-1)
    parser.add_argument("--restricted-now", type=parse_int, default=-1)
    parser.add_argument("--bedtime-active", action="store_true")
    parser.add_argument("--parent-unlock-active", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    paths = AppPaths(Path(args.root))
    request = request_from_args(args, paths)
    request_id = str(request.get("request_id") or "")
    if not request_id:
        raise SystemExit("request_id is required")
    result = result_for(args, request, capabilities(paths))
    output = paths.results / f"{request_id}.json"
    write_json_atomic(output, result)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
