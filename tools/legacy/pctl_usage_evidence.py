#!/usr/bin/env python3
"""Reconcile independent PCTL evidence against the accepted Play Timer scope."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import html
import json

from tools.legacy.usage_probe import NS, compare, counter, dump, projection


def valid_spent(s: dict) -> int | None:
    value = counter(s, "spent_ns")
    return value if value is not None and value >= 0 else None


def transition(before: dict, after: dict) -> dict:
    a, b = valid_spent(before), valid_spent(after)
    result = compare(before, after)
    delta = None if a is None or b is None else b - a
    elapsed = result.get("elapsed_s")
    result.update(direct_valid=a is not None and b is not None,
                  direct_delta_ns=None if delta is None else str(delta),
                  counter_decreased=delta is not None and delta < 0,
                  allowance_relation_before=projection(before, clamp=False) == a if a is not None and projection(before, clamp=False) is not None else None,
                  allowance_relation_after=projection(after, clamp=False) == b if b is not None and projection(after, clamp=False) is not None else None,
                  excessive_jump=delta > (elapsed + 2) * NS if delta is not None and elapsed is not None else None)
    return result


def scoped_report(root: Path, out: Path) -> dict:
    inputs: dict[str, str] = {}
    def read(path: Path) -> dict:
        inputs[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        return json.loads(path.read_text(encoding="utf-8"))
    batches = [("first", root), ("repeat", root / "supplement-20261013"),
               ("clock-ipc", root / "supplement-clock-ipc")]
    runs = {name: read(path / "run.json") for name, path in batches}
    phases = {(name, p["name"]): p for name, run in runs.items() for p in run["phases"]}
    scene_keys = [("game", "first", "application-foreground"),
                  ("HOME", "first", "application-background-home"),
                  ("settings", "repeat", "repeat-system-settings"),
                  ("no-allowance", "first", "no-allowance-home"),
                  ("no-allowance-repeat", "repeat", "repeat-no-allowance"),
                  ("unlimited", "first", "unlimited-home"),
                  ("restricted", "first", "overrun-limit-1")]
    scenes = []
    for label, batch, name in scene_keys:
        p = phases[batch, name]
        scenes.append({"scene": label, "batch": batch, "phase": name, "capture_status": p["status"],
                       "before": p["before"], "after": p["after"], "comparison": transition(p["before"], p["after"])})
    action_rows = []
    for name, run in runs.items():
        for a in run["actions"]:
            if a.get("op") != "limit" or not a.get("ok"):
                continue
            b, c = a["before"], a["after"]
            action_rows.append({"batch": name, "request_id": a["id"], "before": b, "after": c,
                                "quota_delta_minutes": c.get("configured_minutes", 0) - b.get("configured_minutes", 0),
                                "comparison": transition(b, c)})
    grants = [x for x in action_rows if x["batch"] == "repeat" and x["quota_delta_minutes"] == 120]
    mainline = {
        "accepted_scope": "game, HOME and settings; restricted unusable periods may freeze; not all screen-on or game foreground time",
        "scenes": scenes, "quota_transitions": action_rows,
        "consecutive_120_grants": {"status": "verified_while_still_restricted" if len(grants) == 2 and all(x["comparison"]["direct_delta_ns"] == "0" and x["comparison"]["allowance_relation_after"] is True and x["after"]["restricted"] for x in grants) else "incomplete",
                                   "transitions": grants,
                                   "gap": "same continuous +120/+120 chain reaching usable state is not fully validated; first 125-minute dwell was restored to 180 and is excluded"},
        "sleep_wake": {"status": "pending", "reason": "display-off is not sleep; explicit StopPlayTimer is not sleep"},
        "same_day_reboot": {"status": "partial", "reason": "nonzero later samples after human reboots support retention; no bounded before/after reboot pair with reliable uptime proof"},
        "first_takeover": {"status": "partial", "reason": "no-allowance windows and large pre-probe cumulative values exist, but already-enabled PlayWise and cleared slots cannot prove true first takeover"},
        "deferred": ["PDM history", "application active time", "game foreground-only", "multi-account", "midnight/date setters", "PIN unlock", "dock switching", "first all-zero parental-control initialization"],
        "algorithm_assessment": "1952 is a strong direct display candidate for accepted scope; no product implementation in this task; sleep/reboot/true-first-takeover gaps prevent unconditional qualification",
        "input_hashes": inputs,
    }
    cross = root / "safe-continuation/offline-crosscheck.json"
    old = read(cross)
    mainline["previous_crosscheck"] = {k: old[k] for k in ("sample_count", "limited_relation_compared_samples", "exact_match_samples", "max_absolute_relation_error_ns", "negative_remaining_samples", "spent_decreases_in_same_probe_session")}
    dump(out / "evidence.json", mainline)
    lines = ["# PCTL 1952 主线：按新验收口径收敛", "", "范围：游戏、HOME、系统设置期间计时；受限无法正常使用时允许冻结。不要求全部亮屏时间或纯游戏前台。", "", "|已有证据|窗口秒数|1952 增加秒数|范围|", "|---|---:|---:|---|"]
    for s in scenes:
        c = s["comparison"]
        lines.append(f'|{s["scene"]}|{c.get("elapsed_s", "未知")}|{c.get("spent_ns_delta_s", "未知")}|独立原始端点，场景画面此前已核对；无额度为已运行环境|')
    lines += ["", "1770 条可信限时样本全部满足 `额度纳秒 − 有符号1454剩余 = 1952累计`，包含248条负剩余样本。该结果不替代休眠、重启和首次接管验证。", "", "连续两次+120：修正版5→125→245分钟，累计均23176秒，剩余分别−15676和−8476秒，均仍受限。此子场景通过。完整同链恢复正常仍缺证据；旧首轮125分钟窗口自动回到180分钟，排除该固定额度窗口。", "", "三个关键场景实际状态：", "", "- 真休眠/唤醒：未验证。普通关显示及显式stop不能替代。", "- 同日重启：部分证据。人工重启后的非零大累计与保留一致；尚无可信重启前后短间隔配对，不能证明完整连续性。", "- 首次接管：部分证据。已证实空当天额度和不限时仍有累计；已启用后的清空不是启用前首接管。", "", "结论：现有证据支持1952满足所测游戏/HOME/设置及受限冻结行为，支持将其列为显示修正候选；尚不支持把全部生命周期验收标为通过。产品修改另列，不在本轮实现。", "", "原始端点、操作ID、带符号读数及输入哈希见 evidence.json；新硬件读数必须单独标识，不能将本次重分析当作新硬件实验。"]
    (out / "findings.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    body = html.escape("\n".join(lines))
    (out / "index.html").write_text('<!doctype html><meta charset="utf-8"><title>PCTL 1952主线证据</title><style>body{font:16px system-ui;max-width:1100px;margin:24px;background:#f4f7fb;color:#182740}pre{white-space:pre-wrap;line-height:1.6}a{color:#2464cb}</style><h1>PCTL 1952主线证据</h1><p><a href="plan.md">执行计划和通过标准</a> · <a href="evidence.json">精确读数与证据哈希</a></p><pre>' + body + '</pre>', encoding="utf-8")
    return mainline


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    j = scoped_report(a.root, a.out)
    print(json.dumps({"consecutive_120_grants": j["consecutive_120_grants"]["status"],
                      "sleep_wake": j["sleep_wake"]["status"], "same_day_reboot": j["same_day_reboot"]["status"],
                      "first_takeover": j["first_takeover"]["status"]}, ensure_ascii=False))
