#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

LEGACY_CONTRACT = "pctl1952_v1_proxy1440"
DIRECT_CONTRACT = "pctl1952_v2_direct"


# Canonical eye-care qualification observations (key -> operator prompt). The
# authoritative gate (verify_device_qualification.verify_eye_care_report) and the
# guided capture tool (capture_device_observations) both read this list, so the
# template, the prompts and the gate can never drift apart. The 0x44 restore
# facts live in their own `pctl` block below, not among these observations.
OBSERVATION_FIELDS = (
    ("sleep_excludes_usage", "睡眠期间是否不扣使用时长？"),
    ("home_awake_counted", "HOME 亮屏是否计入使用？"),
    ("unlimited_vs_1440_proxy_compared", "是否对比了原不限时 ↔ 临时 1440 代理？"),
    ("rest_start_observed", "是否肉眼观察到休息开始（浮窗出现、游戏暂停）？"),
    ("rest_end_observed", "是否肉眼观察到休息结束并恢复？"),
    ("overlay_pin_after_rest", "家长 PIN 后浮窗是否正常？"),
    ("today_schedule_single_skip", "今日安排单次跳过是否生效？"),
    ("daily_allowance_bedtime_handover", "每日额度与就寝交接是否正确？"),
    ("disable_eye_care_restores_unlimited", "关闭护眼后是否恢复原不限时？"),
    ("expired_break_id_reprompts_not_release", "过期 break_id 是否提示重看而不放行？"),
)

DIRECT_OBSERVATION_FIELDS = tuple(
    ("direct_cycle_observed", "是否观察到真正不限时、护眼受限与同实例自然恢复（无1440代理）？")
    if key == "unlimited_vs_1440_proxy_compared" else (key, prompt)
    for key, prompt in OBSERVATION_FIELDS
)


def observation_fields(usage_contract: str | None = None) -> tuple[tuple[str, str], ...]:
    if usage_contract not in (None, LEGACY_CONTRACT, DIRECT_CONTRACT):
        raise ValueError("unsupported eye-care usage contract")
    return DIRECT_OBSERVATION_FIELDS if usage_contract == DIRECT_CONTRACT else OBSERVATION_FIELDS


def template(commit: str, release_id: str, model: str, hos: str, atmosphere: str,
             usage_contract: str | None = None) -> dict:
    result = {
        "schema_version": 2 if usage_contract == DIRECT_CONTRACT else 1,
        "report_status": "draft",
        "subject": {"commit": commit, "release_id": release_id},
        "environment": {"model": model, "hos": hos, "atmosphere": atmosphere},
        "games": ["", ""],
        "timing": {"rest_start_trigger_seconds": None, "rest_end_trigger_seconds": None},
        "observations": {key: None for key, _ in observation_fields(usage_contract)},
        "pctl": {
            "settings_exactly_restored": None,
            "unexpected_raw_offsets": None,
            "eye_care_consumed_daily_allowance": None,
        },
    }
    if usage_contract is not None:
        result["usage_contract"] = usage_contract
    if usage_contract == DIRECT_CONTRACT:
        result["pctl_usage_report"] = {"path": "pctl-usage-qualification.json", "sha256": None}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 eye-care 真机资格报告草稿。")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--release-id", required=True)
    parser.add_argument("--model", default="mariko-oled")
    parser.add_argument("--hos", default="22.5.0")
    parser.add_argument("--atmosphere", default="1.11.2")
    parser.add_argument("--usage-contract", choices=(LEGACY_CONTRACT, DIRECT_CONTRACT),
                        help="去代理候选必须显式选择 pctl1952_v2_direct；省略时保留旧schema v1")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists; preserve existing evidence")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(template(args.commit, args.release_id, args.model, args.hos, args.atmosphere, args.usage_contract),
                   ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
