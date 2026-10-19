#!/usr/bin/env python3
"""Guided capture of the irreducibly-manual Device Lab observations.

Prompts the operator item-by-item (screen observation + button holds the tool
can never do itself) and writes structured JSON:
  * bedtime-qualification.json -- fills the make_bedtime template so the
    authoritative gate (verify_device_qualification.verify_bedtime_report) can
    consume it.
  * eye-care-qualification.json -- fills the make_eye_care template so the
    authoritative gate (verify_device_qualification.verify_eye_care_report) can
    consume it (opt-in via --require-eye-care; eye-care is a candidate feature).
  * standard-observations.json -- advisory eyeball items from the standard
    acceptance checklist.
"""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json


import tools.qualification.make_bedtime_qualification_template as make_bedtime  # noqa: E402
import tools.qualification.make_eye_care_qualification_template as make_eye_care  # noqa: E402


class CaptureError(RuntimeError):
    pass


def configure_console() -> None:
    # Only soften the error handler; do not force an encoding, so output stays
    # in the locale codec that a capturing parent (tools/test.py) decodes with.
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            try:
                reconfigure(errors="replace")
            except (ValueError, OSError):
                pass


def _prompt(reader, text: str) -> str:
    try:
        return reader(text)
    except (EOFError, StopIteration):
        raise CaptureError("输入提前结束")


def ask_bool(reader, text: str, attempts: int = 3) -> bool:
    for _ in range(attempts):
        answer = _prompt(reader, f"{text} [y/n]: ").strip().lower()
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        print("  请输入 y 或 n。")
    raise CaptureError(f"布尔输入无效：{text}")

def ask_int(reader, text: str, low: int, high: int, attempts: int = 3) -> int:
    for _ in range(attempts):
        answer = _prompt(reader, f"{text} [{low}-{high}]: ").strip()
        try:
            value = int(answer)
        except ValueError:
            print("  请输入整数。")
            continue
        if low <= value <= high:
            return value
        print(f"  超出范围 {low}-{high}。")
    raise CaptureError(f"数值输入无效：{text}")


def ask_text(reader, text: str, attempts: int = 3) -> str:
    for _ in range(attempts):
        answer = _prompt(reader, f"{text}: ").strip()
        if answer:
            return answer
        print("  不能为空。")
    raise CaptureError(f"文本输入无效：{text}")


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8", newline="\n")

def capture_bedtime(reader, commit: str, release_id: str, model: str, hos: str, atmosphere: str) -> dict:
    report = make_bedtime.template(commit, release_id, model, hos, atmosphere)
    report["report_status"] = "final"
    report["games"] = [ask_text(reader, "游戏 A 名称"), ask_text(reader, "游戏 B 名称")]
    report["timing"]["boundary_trigger_seconds"] = ask_int(reader, "到点触发秒数", 0, 60)
    report["timing"]["wake_trigger_seconds"] = ask_int(reader, "窗口内唤醒触发秒数", 0, 60)
    for entry in ("game", "homebrew", "home", "system_settings", "playwise_nro"):
        report["blocked_entries"][entry] = ask_bool(reader, f"弹窗期间 {entry} 入口是否被阻断？")
    overlay = report["overlay"]
    overlay["opened_during_popup"] = ask_bool(reader, "弹窗期间 Overlay 能否打开？")
    overlay["pin_verified"] = ask_bool(reader, "Overlay PIN 是否验证通过？")
    overlay["shared_cooldown_verified"] = ask_bool(reader, "是否共享 NRO 的 PIN/错误次数/冷却？")
    overlay["single_action_authorization_verified"] = ask_bool(reader, "单次授权是否生效？")
    overlay["daily_limit_message_verified"] = ask_bool(reader, "每日额度仍生效时是否显示提示？")
    for action in ("skip_instance", "disable_bedtime", "restore_install_snapshot"):
        recovery = overlay["recovery"][action]
        recovery["request_submitted"] = ask_bool(reader, f"{action}：是否提交请求？")
        recovery["pctl_reread"] = ask_bool(reader, f"{action}：是否重读 PCTL？")
        recovery["popup_cleared"] = ask_bool(reader, f"{action}：弹窗是否解除？")
    failure = overlay["backend_failure"]
    failure["reported_unconfirmed"] = ask_bool(reader, "后台失联时是否显示“请求未确认”？")
    failure["external_recovery_shown"] = ask_bool(reader, "是否显示外部恢复说明？")
    failure["startup_restore_flag_created"] = ask_bool(reader, "是否产生 restore_install_snapshot.flag？（应为否）")
    pause = report["official_pause"]
    pause["on_recorded"] = ask_bool(reader, "官方暂停开关 ON 是否记录？")
    pause["off_recorded"] = ask_bool(reader, "官方暂停开关 OFF 是否记录？")
    pause["playwise_modified_setting"] = ask_bool(reader, "PlayWise 是否改动了该设置？（应为否）")
    for phase in ("home", "foreground", "suspend", "sleep", "reboot", "cross_day",
                  "manual_clock_change", "recovery_failure"):
        report["lifecycle"][phase] = ask_bool(reader, f"生命周期 {phase} 是否通过？")
    report["pctl"]["settings_exactly_restored"] = ask_bool(reader, "0x44 是否逐字节精确恢复？")
    report["pctl"]["unexpected_raw_offsets"] = []
    report["pctl"]["bedtime_consumed_daily_allowance"] = ask_bool(reader, "bedtime 是否消耗了每日额度？（应为否）")
    missing = report["missing_overlay"]
    missing["warning_unskippable"] = ask_bool(reader, "缺 Overlay 时高风险提示是否不可跳过？")
    missing["pin_and_long_hold_required"] = ask_bool(reader, "是否要求 PIN + 长按？")
    missing["enable_allowed_after_confirmation"] = ask_bool(reader, "确认后是否允许启用？")
    missing["schedule_continues_without_handshake"] = ask_bool(reader, "无握手是否仍继续计划（防绕过）？")
    return report

STANDARD_FIELDS = (
    ("pin_created", "是否创建数字 PIN 且 secret 非占位值？"),
    ("takeover_confirmed", "家长确认接管后是否进入 Active？"),
    ("temp_unlock_sleep_restores", "临时解除后睡眠唤醒是否恢复限制？"),
    ("reboot_persistence", "重启后 setup/rules/state/ledger/事务是否恢复？"),
    ("disable_flag_fuses_writes", "disable.flag 下是否只熔断写入、保留诊断/恢复？"),
    ("theme_instant_repaint", "切换主题是否立即重绘并在重启后保留？"),
    ("dock_replug_2s", "拔插底座后是否 2 秒内进入/恢复限制？"),
    ("overdraft_not_bypassed", "加时是否不绕过额度？"),
)


def capture_eye_care(reader, commit: str, release_id: str, model: str, hos: str, atmosphere: str,
                     usage_contract: str | None = None, usage_report_sha256: str | None = None) -> dict:
    if usage_contract == make_eye_care.DIRECT_CONTRACT and usage_report_sha256 is None:
        raise CaptureError("去代理观察需要同目录final标准1952资格报告")
    report = make_eye_care.template(commit, release_id, model, hos, atmosphere, usage_contract)
    if usage_contract == make_eye_care.DIRECT_CONTRACT:
        report["pctl_usage_report"]["sha256"] = usage_report_sha256
    report["report_status"] = "final"
    report["games"] = [ask_text(reader, "游戏 A 名称"), ask_text(reader, "游戏 B 名称")]
    report["timing"]["rest_start_trigger_seconds"] = ask_int(reader, "休息开始触发秒数", 0, 60)
    report["timing"]["rest_end_trigger_seconds"] = ask_int(reader, "休息恢复触发秒数", 0, 60)
    for key, prompt in make_eye_care.observation_fields(usage_contract):
        report["observations"][key] = ask_bool(reader, prompt)
    report["pctl"]["settings_exactly_restored"] = ask_bool(reader, "0x44 是否逐字节精确恢复？")
    report["pctl"]["unexpected_raw_offsets"] = []
    report["pctl"]["eye_care_consumed_daily_allowance"] = ask_bool(reader, "护眼休息是否消耗了每日额度？（应为否）")
    return report


def direct_usage_digest(reports: Path, commit: str, release_id: str) -> str:
    """Bind existing bytes; the authoritative gate still verifies all raw and artifacts."""
    try:
        raw = (reports / "pctl-usage-qualification.json").read_bytes()
        value = json.loads(raw)
    except (OSError, ValueError) as exc:
        raise CaptureError("缺少可读取的final标准1952资格报告") from exc
    if not isinstance(value, dict) or value.get("report_status") != "final" or \
            value.get("subject") != {"commit": commit, "release_id": release_id} or \
            value.get("usage_contract") != make_eye_care.DIRECT_CONTRACT:
        raise CaptureError("标准1952资格的final状态、候选或去代理契约不匹配")
    return hashlib.sha256(raw).hexdigest()


def capture_standard(reader, commit: str, release_id: str) -> dict:
    report = {
        "schema_version": 1,
        "report_status": "final",
        "advisory": True,
        "subject": {"commit": commit, "release_id": release_id},
        "observations": {},
    }
    for key, prompt in STANDARD_FIELDS:
        report["observations"][key] = ask_bool(reader, prompt)
    return report

MANUAL_PREFACE = (
    "本工具只收集肉眼观察与按键结果，无法代替任何设备操作。请确认已完成：",
    "  - 实体手柄长按 ZL+ZR+A 启用实验后台",
    "  - 危险阶段实体手柄长按 A 两秒（触摸无效）",
    "  - 关 Overlay 肉眼观察约 15 秒限制提示与游戏行为",
    "  - 需要时用 Nintendo 官方 PIN 改系统时钟做 bedtime 边界",
)


def print_preface() -> None:
    print("=" * 64)
    for line in MANUAL_PREFACE:
        print(line)
    print("=" * 64)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="采集 Device Lab 人工观察并写出结构化 JSON。")
    parser.add_argument("kind", choices=["bedtime", "eye-care", "standard"])
    parser.add_argument("--commit", required=True)
    parser.add_argument("--release-id", required=True)
    parser.add_argument("--model", default="mariko-oled")
    parser.add_argument("--hos", default="22.5.0")
    parser.add_argument("--atmosphere", default="1.11.2")
    parser.add_argument("--reports", type=Path, default=Path("device-reports"))
    parser.add_argument("--usage-contract", choices=(make_eye_care.LEGACY_CONTRACT, make_eye_care.DIRECT_CONTRACT),
                        help="eye-care去代理候选使用 pctl1952_v2_direct；省略保留旧观察契约")
    return parser


def main(argv: list[str] | None = None, reader=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if reader is None:
        reader = input
    configure_console()
    print_preface()
    try:
        if args.kind == "bedtime":
            report = capture_bedtime(reader, args.commit, args.release_id, args.model, args.hos, args.atmosphere)
            out = args.reports / "bedtime-qualification.json"
        elif args.kind == "eye-care":
            out = args.reports / "eye-care-qualification.json"
            if out.exists():
                raise CaptureError("输出已存在，保留原观察报告")
            usage_digest = direct_usage_digest(args.reports, args.commit, args.release_id) \
                if args.usage_contract == make_eye_care.DIRECT_CONTRACT else None
            report = capture_eye_care(reader, args.commit, args.release_id, args.model, args.hos, args.atmosphere,
                                      args.usage_contract, usage_digest)
        else:
            report = capture_standard(reader, args.commit, args.release_id)
            out = args.reports / "standard-observations.json"
    except CaptureError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    write_json(out, report)
    print(f"已写入 {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
