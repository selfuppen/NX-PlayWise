#!/usr/bin/env python3
"""Natural midnight evidence; native reset time is observed, never assumed."""
from __future__ import annotations

import argparse
import hashlib
import html
import json
from pathlib import Path

from pctl_usage_evidence import valid_spent
from usage_probe import dump, projection


def analyze(rows: list[dict], target_unix_s: int) -> dict:
    groups: dict[str, list[dict]] = {}
    errors, negative, mismatches = [], [], []
    for s in rows:
        groups.setdefault(s["boot_id"], []).append(s)
        if any(s.get(k) != 0 for k in ("sample_error", "spent_result", "remaining_result", "settings_result", "timer_result", "restricted_result")):
            errors.append(s)
        if s.get("spent_result") == 0 and int(s["spent_ns"]) < 0:
            negative.append(s)
        spent = valid_spent(s)
        signed = projection(s, clamp=False)
        if spent is not None and signed is not None and signed != spent:
            mismatches.append({"sample": s, "difference_ns": str(signed-spent)})
    boundaries, drops = [], []
    for group in groups.values():
        group.sort(key=lambda s: int(s["tick_ns"]))
        for a, b in zip(group, group[1:]):
            av, bv = valid_spent(a), valid_spent(b)
            wall = int(b["unix_s"])-int(a["unix_s"])
            if av is not None and bv is not None and bv < av:
                drops.append({"before": a, "after": b, "same_local_day": a["day_index"] == b["day_index"]})
            if a["day_index"] != b["day_index"]:
                boundaries.append({"before": a, "after": b, "wall_interval_s": wall,
                                   "date_increment": b["day_index"]-a["day_index"],
                                   "first_new_date_offset_from_host_midnight_s": int(b["unix_s"])-target_unix_s,
                                   "bounded_same_boot_boundary": 0 <= wall <= 15 and b["day_index"] == a["day_index"]+1,
                                   "counter_delta_s": None if av is None or bv is None else (bv-av)/1e9,
                                   "counter_reset_to_zero_at_boundary": None if bv is None else bv == 0})
    later = [s for s in rows if int(s["unix_s"]) >= target_unix_s+60 and valid_spent(s) is not None]
    usable = [s for s in later if s.get("timer_enabled") and not s.get("restricted")]
    return {"sample_count": len(rows), "boot_count": len(groups), "boundaries": boundaries,
            "read_error_rows": errors, "negative_direct_rows": negative,
            "signed_relation_mismatches": mismatches, "counter_drops": drops,
            "next_day_growth": "requires_independent_window" if usable else "not_tested_while_bedtime_restricted",
            "status": "bounded_natural_date_transition_observed" if any(b["bounded_same_boot_boundary"] for b in boundaries) else "incomplete_no_bounded_same_boot_date_transition",
            "note": "observed reset or non-reset; no inferred native midnight reset contract; rule/status qualification recorded separately"}


def report(out: Path) -> dict:
    plan = json.loads((out/"plan.json").read_text(encoding="utf-8"))
    rows = json.loads((out/"normalized-samples.json").read_text(encoding="utf-8"))
    result = analyze(rows, plan["target_midnight_unix_s"])
    for phase in ("before_midnight", "after_midnight"):
        path = out/(phase+"-status.json")
        if path.exists():
            result[phase+"_status"] = json.loads(path.read_text(encoding="utf-8"))
    before_state = result.get("before_midnight_status", {}).get("state", {})
    after_state = result.get("after_midnight_status", {}).get("state", {})
    if before_state and after_state:
        expected = next((f for f in before_state.get("forecast", []) if f["day_index"] == after_state["day_index"]), None)
        actual = after_state["forecast"][0]
        result["rule_verification"] = {"expected_next_day": expected, "actual_new_day": actual,
            "logical_quota_matches_prior_forecast": expected == actual,
            "same_overnight_bedtime_instance": before_state["bedtime"]["window_instance_id"] == after_state["bedtime"]["window_instance_id"],
            "bedtime_enforced": after_state["bedtime"]["active"] and not after_state["bedtime"]["skipped"] and after_state["restricted_now"] == 1 and after_state["bedtime"]["official_setting_confirmed"],
            "base_60_minutes_actual_unrestricted_application": "pending_bedtime_override_active"}
    if (out/"watcher-result.json").exists():
        result["watcher_result"] = json.loads((out/"watcher-result.json").read_text(encoding="utf-8"))
    if (out/"final-audit.json").exists():
        result["final_audit"] = json.loads((out/"final-audit.json").read_text(encoding="utf-8"))
    visual_path = out/"manual-visual-review.json"
    visual = json.loads(visual_path.read_text(encoding="utf-8")) if visual_path.exists() else {"observations": []}
    result["manual_visual_review"] = visual
    visual_by_image = {entry["image"]: entry for entry in visual["observations"]}
    identity = json.loads((out/"capture-install.json").read_text(encoding="utf-8"))
    result["capture_identity"] = {k: identity[k] for k in ("run_id", "source_hash", "phase", "original_sha256", "capture_sha256")}
    names = ["plan.json", "capture-install.json", "normalized-samples.json", "collection.json", "manual-attestation.json", "manual-visual-review.json", "before_midnight-status.json", "after_midnight-status.json", "watcher-result.json", "final-audit.json"]
    result["input_files_sha256"] = {name: hashlib.sha256((out/name).read_bytes()).hexdigest() for name in names if (out/name).exists()}
    dump(out/"midnight-evidence.json", result)
    collecting = not (out/"watcher-result.json").exists()
    lines = ["# 自然跨天补测", "", "这是原主线之外的可选补测，不改系统日期，不将就寝停表当作次日增长验证。", "", "采集中：下面仅为当前已下载快照，尚不代表最终结论。" if collecting else "采集结束，下面为实际结果。", "", f'取证状态：{result["status"]}；{result["sample_count"]}条样本，{result["boot_count"]}个启动会话。', ""]
    for b in result["boundaries"]:
        a, c = b["before"], b["after"]
        lines += [f'- 日期{a["day_index"]}→{c["day_index"]}，同启动原始端点间隔{b["wall_interval_s"]}秒。', f'- 1952：{int(a["spent_ns"])/1e9:g}→{int(c["spent_ns"])/1e9:g}秒；日期切换端点是否归零：{b["counter_reset_to_zero_at_boundary"]}。', f'- 1454带符号剩余：{int(a["remaining_ns"])/1e9:g}→{int(c["remaining_ns"])/1e9:g}秒；PCTL实际模式：{a["settings_mode"]}→{c["settings_mode"]}。', ""]
    lines += [f'读取错误{len(result["read_error_rows"])}条，负累计{len(result["negative_direct_rows"])}条，有符号关系不一致{len(result["signed_relation_mismatches"])}条。异常及计数下降保留全部原始端点，不清零填补或忽略。', "", "逻辑基础额度与就寝0分钟执行覆盖分开核对："]
    for phase in ("before_midnight", "after_midnight"):
        r = result.get(phase+"_status", {})
        s = r.get("state")
        if s:
            lines.append(f'- {phase}：day={s["day_index"]}，forecast今日={json.dumps(s["forecast"][0], ensure_ascii=False)}；bedtime active={s["bedtime"]["active"]}，skipped={s["bedtime"]["skipped"]}，restricted={s["restricted_now"]}，timer={s["play_timer_enabled"]}。')
    lines += ["", f'次日可用增长：{result["next_day_growth"]}。午夜时停表只证明该受限窗口，不能证明次日游戏/HOME/设置增长。', "", "自然日期切换时未归零不自动判为服务缺陷：本实验没有预设原生累计统计日边界。若累计未归零，须另验证何时重置及自然日显示如何归属，不能直接把1952原值当作次日已用。", "", "精确Result、raw 0x44、时间戳、启动身份和异常见 [midnight-evidence.json](midnight-evidence.json)。恢复状态另见 watcher-result.json；人工画面观察单独保存，不改写原始附件。"]
    if "rule_verification" in result:
        check = result["rule_verification"]
        lines += ["", f'次日逻辑基础额度与此前预测一致={check["logical_quota_matches_prior_forecast"]}；同一跨夜就寝实例={check["same_overnight_bedtime_instance"]}；就寝执行覆盖确认={check["bedtime_enforced"]}。60分钟基础额度解除就寝后的实际可用执行仍待补，不把forecast当作完整设备额度验收。']
    later_path = out.parent/"next-day-growth/next-day-result.json"
    if later_path.exists():
        later = json.loads(later_path.read_text(encoding="utf-8"))
        result["later_next_day_supplement"] = later
        result["input_files_sha256"]["../next-day-growth/next-day-result.json"] = hashlib.sha256(later_path.read_bytes()).hexdigest()
        if later["status"] == "passed_actual_base_quota_and_next_day_growth":
            c = later["counting_phase"]["comparison"]
            lines += ["", f'后续独立批次已补齐午夜窗口遗留的次日可用增长：实际60分钟额度下{c["elapsed_s"]:.2f}秒增加{c["spent_ns_delta_s"]:g}秒，带符号1454吻合。上面的pending仅描述当时受限的跨午夜窗口；完整60分钟耗尽阈值未跑。见 [次日报告](../next-day-growth/index.html)。']
    dump(out/"midnight-evidence.json", result)
    finished = result.get("watcher_result")
    if finished:
        lines += ["", "收尾记录：", "", "```json", json.dumps(finished, ensure_ascii=False, indent=2), "```"]
    if "final_audit" in result:
        audit = result["final_audit"]
        lines += ["", f'次日读数稳定性：{audit["next_day_samples"]}条样本，累计范围{audit["next_day_spent_range_s"]}秒；最后读数unix={audit["last_next_day_unix_s"]}。原像哈希已核验，删除采集配置后原始文件连续6秒不再追加。']
    lines += ["", "## 画面人工观察", ""]
    for observation in visual["observations"]:
        if observation.get("status") == "human_confirmed":
            lines.append(f'- {observation["id"]}：{observation["observation"]}')
    (out/"findings.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    body = '<!doctype html><meta charset="utf-8"><title>自然跨天取证</title><style>body{font:16px system-ui;max-width:1100px;margin:24px;background:#f4f7fb;color:#182740}pre{white-space:pre-wrap;line-height:1.65}img{max-width:900px;width:100%}</style><p><a href="../index.html">原主线报告</a> · <a href="midnight-evidence.json">原始端点与异常</a></p><pre>'+html.escape("\n".join(lines))+'</pre>'
    for phase in ("before_midnight", "after_midnight"):
        if (out/"screenshots"/(phase+".jpg")).exists():
            observation = visual_by_image.get("screenshots/"+phase+".jpg")
            confirmed = observation is not None and observation.get("status") == "human_confirmed"
            state = "已人工确认" if confirmed else "待统一人工核对"
            body += '<h2>'+html.escape(phase)+'，'+state+'</h2>'
            if confirmed:
                body += '<p>'+html.escape(observation["observation"])+'</p>'
            body += '<img src="screenshots/'+phase+'.jpg">'
    (out/"index.html").write_text(body, encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    result = report(parser.parse_args().out)
    print(json.dumps({k: result[k] for k in ("status", "sample_count", "next_day_growth")}, ensure_ascii=False))
