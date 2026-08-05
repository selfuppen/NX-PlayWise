#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import asdict
import time
from pathlib import Path
from typing import Any

from ptc_request_queue import AppPaths, archive_done, list_pending, move_to_processing, read_json, write_json_atomic
from ptc_token_v1 import TokenError, verify_token as verify_token_v1
from ptc_token_v2 import verify_token as verify_token_v2


ERRORS: dict[str, tuple[int, str]] = {
    "unsupported_version": (100, "不支持的协议版本"),
    "bad_request": (101, "请求格式无效"),
    "unknown_request_type": (102, "未知请求类型"),
    "bad_code": (200, "授权码格式无效"),
    "bad_token_version": (201, "授权码版本不支持"),
    "unsupported_action": (202, "授权码动作不支持"),
    "bad_signature": (203, "授权码签名不匹配"),
    "wrong_date": (205, "授权码不是今天有效"),
    "used_token": (206, "授权码已经使用过"),
    "minutes_exceed_limit": (207, "授权分钟数超过上限"),
    "code_cooldown": (208, "短码错误次数过多，请稍后再试"),
    "disabled": (300, "后台当前已禁用"),
    "config_invalid": (502, "配置文件无效"),
}


class ObserveProcessor:
    def __init__(self, sdmc_root: Path, current_day_index: int):
        self.paths = AppPaths(sdmc_root)
        self.current_day_index = current_day_index

    def process_all(self) -> int:
        count = 0
        for pending in list_pending(self.paths):
            self.process_file(pending)
            count += 1
        return count

    def process_file(self, pending_file: Path) -> None:
        processing = move_to_processing(self.paths, pending_file)
        request_id = processing.stem
        request_type = "unknown"
        try:
            request = read_json(processing)
            request_id = str(request.get("request_id") or request_id)
            request_type = str(request.get("type") or "unknown")
            result = self.process_request(request)
        except Exception as exc:
            result = self.error_result(request_id, request_type, "bad_request", detail=str(exc))

        write_json_atomic(self.paths.results / f"{request_id}.json", result)
        archive_done(self.paths, processing)

    def process_request(self, request: dict[str, Any]) -> dict[str, Any]:
        if request.get("version") != 1:
            return self.error_result(str(request.get("request_id", "unknown")), str(request.get("type", "unknown")), "unsupported_version")

        request_id = str(request.get("request_id") or "")
        request_type = str(request.get("type") or "")
        payload = request.get("payload") if isinstance(request.get("payload"), dict) else {}

        config = self.load_config()
        mode = str(config.get("control_mode", "observe"))
        if mode == "disabled":
            return self.error_result(request_id, request_type, "disabled", mode="disabled")
        if mode not in {"observe", "grant", "enforce"}:
            mode = "observe"

        if request_type == "status":
            return self.ok_result(request_id, request_type, mode, state=self.base_state())
        if request_type == "offline_code":
            return self.handle_offline_code(request_id, request_type, payload, config, mode)
        return self.error_result(request_id, request_type, "unknown_request_type", mode=mode)

    def handle_offline_code(
        self,
        request_id: str,
        request_type: str,
        payload: dict[str, Any],
        config: dict[str, Any],
        mode: str,
    ) -> dict[str, Any]:
        code = str(payload.get("code") or "")
        used_nonces: set[tuple[int, int]] = set()
        try:
            is_v2 = "-" not in code and (
                len(code) == 8 or (code.isascii() and code.isdigit() and len(code) not in {0, 16})
            )
            verifier = verify_token_v2 if is_v2 else verify_token_v1
            token = verifier(
                code,
                device_id=str(config["device_id"]),
                secret=str(config["grant_secret"]),
                current_day_index=self.current_day_index,
                max_add_minutes=int(config.get("max_add_minutes", 120)),
                used_nonces=used_nonces,
            )
        except KeyError:
            return self.error_result(request_id, request_type, "config_invalid", mode=mode)
        except TokenError as exc:
            return self.error_result(request_id, request_type, exc.reason, mode=mode)

        state = self.base_state()
        state.update(
            {
                "active_rule": "unknown",
                "pending_apply": True,
                "requested_minutes": token.minutes,
            }
        )
        return self.ok_result(
            request_id,
            request_type,
            mode,
            applied={
                "minutes": token.minutes,
                "today_limit": None,
                "pending_apply": True,
                "token": asdict(token),
            },
            state=state,
        )

    def load_config(self) -> dict[str, Any]:
        return read_json(self.paths.app_root / "config.json")

    def base_state(self) -> dict[str, Any]:
        return {
            "day_index": self.current_day_index,
            "limited_today": None,
            "blocked_today": None,
            "unrestricted_today": None,
            "remaining_available": False,
            "remaining_minutes": None,
            "play_timer_enabled": None,
            "restricted_now": None,
            "bedtime_active": False,
            "parent_unlock_active": False,
        }

    def capabilities(self) -> dict[str, Any]:
        path = self.paths.app_root / "capabilities.json"
        if path.exists():
            data = read_json(path)
            return {
                "raw_block_verified": bool(data.get("raw_block_verified", False)),
                "suspend_verified": bool(data.get("suspend_verified", False)),
            }
        return {"raw_block_verified": False, "suspend_verified": False}

    def ok_result(
        self,
        request_id: str,
        request_type: str,
        mode: str,
        applied: dict[str, Any] | None = None,
        state: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        result = {
            "version": 1,
            "request_id": request_id,
            "type": request_type,
            "status": "ok",
            "mode": mode,
            "dry_run": True,
            "state": state or self.base_state(),
            "capabilities": self.capabilities(),
            "completed_at": int(time.time()),
        }
        if applied is not None:
            result["applied"] = applied
        return result

    def error_result(
        self,
        request_id: str,
        request_type: str,
        reason: str,
        mode: str = "observe",
        detail: str | None = None,
    ) -> dict[str, Any]:
        code, message = ERRORS.get(reason, ERRORS["bad_request"])
        error = {"code": code, "reason": reason, "message": message}
        if detail:
            error["detail"] = detail
        return {
            "version": 1,
            "request_id": request_id,
            "type": request_type,
            "status": "error",
            "mode": mode,
            "dry_run": True,
            "error": error,
            "state": self.base_state(),
            "capabilities": self.capabilities(),
            "completed_at": int(time.time()),
        }

