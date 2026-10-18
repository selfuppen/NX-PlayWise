#!/usr/bin/env python3
"""Bounded next-day usage window under the existing native 60-minute rule."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import html
import json
import time

from tools.legacy.pctl_capture import restore
from tools.legacy.pctl_usage_scenarios import Mainline
from tools.legacy.usage_probe import Device, RELEASE, dump


def require_usable(sample: dict, expected_day: int, minutes: int) -> None:
    results = ("sample_error", "spent_result", "remaining_result", "timer_result", "restricted_result", "settings_result")
    if any(sample.get(key) != 0 for key in results):
        raise RuntimeError("incomplete direct PCTL reads; do not qualify a usable window")
    if sample["day_index"] != expected_day or int(sample["spent_ns"]) < 0:
        raise RuntimeError("wrong local day or invalid cumulative value")
    if not sample.get("configured_available") or sample.get("settings_mode") != "limited" or sample.get("configured_minutes") != minutes:
        raise RuntimeError("native quota differs from expected base rule")
    if not sample.get("timer_enabled") or sample.get("restricted"):
        raise RuntimeError("native timer not running in an unrestricted state")
    if minutes*60*10**9-int(sample["remaining_ns"]) != int(sample["spent_ns"]):
        raise RuntimeError("signed remaining and cumulative counter disagree")


def run(d: Device, out: Path) -> dict:
    plan = json.loads((out/"plan.json").read_text(encoding="utf-8"))
    task = Mainline(d, out)
    original_rules = json.loads((out/"rules-preimage.json").read_text(encoding="utf-8"))
    result: dict = {"status": "running", "quota_minutes": 60, "full_60_minute_exhaustion_tested": False,
                   "true_first_takeover_proven": False, "host_date_write": False}
    try:
        baseline = task.sample()
        if baseline["boot_id"] == task.install["runtime_before"]["boot_id"]:
            raise RuntimeError("new capture not loaded; normal reboot required")
        if baseline["day_index"] != plan["day_index"]:
            raise RuntimeError("local date changed after preparation")
        state = task.request("status")["state"]
        if state["day_index"] != plan["day_index"] or not state.get("restriction_enabled") or state.get("temporary_unlocked"):
            raise RuntimeError("parental control/day precondition changed or PIN bypass active")
        forecast = state["forecast"][0]
        if forecast["mode"] != 1 or forecast["minutes"] != 60:
            raise RuntimeError("next-day logical base rule no longer 60 minutes")
        bedtime = state["bedtime"]
        if bedtime["active"] and not bedtime["skipped"]:
            if bedtime["window_instance_id"] != plan["bedtime_window_instance_id"]:
                raise RuntimeError("bedtime instance changed; do not skip another window")
            task.action("skip_bedtime", {"window_instance_id": plan["bedtime_window_instance_id"]})
        usable = task.sample()
        require_usable(usable, plan["day_index"], 60)
        dump(out/"usable-status.json", task.request("status"))
        task.picture("next-day-60-usable")
        phase = task.phase("next-day-60-minute-rule-counting", plan["counting_window_s"])
        require_usable(phase["before"], plan["day_index"], 60)
        require_usable(phase["after"], plan["day_index"], 60)
        c = phase["comparison"]
        if not c["same_boot"] or not c["settings_stable"] or c["elapsed_s"] < 60 or abs(c["spent_ns_delta_s"]-c["elapsed_s"]) > 2:
            raise RuntimeError("growth not consistent with a continuous usable interval")
        result.update(status="passed_actual_base_quota_and_next_day_growth", baseline=baseline,
                      usable_sample=usable, counting_phase=phase)
    except Exception as exc:
        result.update(status="incomplete", error=type(exc).__name__+": "+str(exc))
    finally:
        owned = task.operations[task.operation_start:]
        ambiguous = [op["request"]["request_id"] for op in owned if op["request"]["type"] != "status" and op["outcome"] in ("pending", "unknown_no_replay")]
        try:
            if ambiguous:
                result["policy_restoration_pending_unknown_request"] = ambiguous
            elif any(op.get("result", {}).get("status") == "ok" and op["request"]["type"] == "skip_bedtime" for op in owned):
                task.request("clear_bedtime_skip", {"window_instance_id": plan["bedtime_window_instance_id"]})
                time.sleep(7)
                final = task.request("status")
                dump(out/"restored-status.json", final)
                state = final["state"]
                if state["bedtime"]["skipped"] or not state["bedtime"]["active"] or state["restricted_now"] != 1:
                    raise RuntimeError("bedtime restoration not confirmed")
                task.picture("next-day-bedtime-restored")
                task.phase("next-day-bedtime-refrozen", 10)
            current_rules = d.json(RELEASE+"/rules.json")
            result["rules_semantically_equal"] = current_rules == original_rules
            if not result["rules_semantically_equal"]:
                raise RuntimeError("rules changed; preserved preimage, no blind overwrite")
            task.sample()
        except Exception as exc:
            result["policy_restoration_error"] = type(exc).__name__+": "+str(exc)
        try:
            journal = json.loads((out/"capture-install.json").read_text(encoding="utf-8"))
            result["backend_restoration"] = restore(d, out, journal)["phase"]
        except Exception as exc:
            result["backend_restoration_error"] = type(exc).__name__+": "+str(exc)
        dump(out/"next-day-result.json", result)
    return result


def report(out: Path) -> dict:
    result = json.loads((out/"next-day-result.json").read_text(encoding="utf-8"))
    journal = json.loads((out/"mainline-run.json").read_text(encoding="utf-8"))
    install = json.loads((out/"capture-install.json").read_text(encoding="utf-8"))
    review_path = out/"manual-visual-review.json"
    visual = json.loads(review_path.read_text(encoding="utf-8")) if review_path.exists() else {"observations": []}
    visual_by_image = {e["image"]: e for e in visual["observations"]}
    names = ["plan.json", "capture-install.json", "mainline-run.json", "next-day-result.json", "collection.json", "normalized-samples.json", "manual-attestation.json", "manual-visual-review.json", "final-audit.json"]
    evidence = {"result": result, "operations_and_phases": journal,
                "capture_identity": {k: install[k] for k in ("run_id", "source_hash", "original_sha256", "capture_sha256", "phase")},
                "manual_visual_review": visual,
                "input_files_sha256": {n: hashlib.sha256((out/n).read_bytes()).hexdigest() for n in names if (out/n).exists()}}
    audit = out/"final-audit.json"
    if audit.exists():
        evidence["final_audit"] = json.loads(audit.read_text(encoding="utf-8"))
    dump(out/"next-day-evidence.json", evidence)
    lines = ["# 次日60分钟额度与继续累计", "", "范围：自然跨天后原60分钟基础规则实际应用及短窗口正常累计；没有修改日期，没有将短窗口写为完整60分钟耗尽验证。", "", "取证状态："+result["status"], ""]
    phase = result.get("counting_phase")
    if phase:
        a, b, c = phase["before"], phase["after"], phase["comparison"]
        lines += [f'- 实际PCTL额度：{a["configured_minutes"]}→{b["configured_minutes"]}分钟，设置保持不变。',
                  f'- 同启动窗口：{c["elapsed_s"]:.2f}秒，1952累计{int(a["spent_ns"])/1e9:g}→{int(b["spent_ns"])/1e9:g}秒，增加{c["spent_ns_delta_s"]:g}秒。',
                  f'- 1454带符号剩余：{int(a["remaining_ns"])/1e9:g}→{int(b["remaining_ns"])/1e9:g}秒；额度纳秒减原始剩余与1952一致。',
                  '- 窗口端点为同一自然日、计时器运行且未受限；未把就寝停表当作次日增长。', ""]
    for p in journal["phases"]:
        if p["name"] == "next-day-bedtime-refrozen" and p["status"] == "captured":
            lines += [f'恢复就寝后{p["comparison"]["elapsed_s"]:.2f}秒，累计增加{p["comparison"]["spent_ns_delta_s"]:g}秒。', ""]
    lines += ["本次次日批次未验证首次接管：此设备已有接管历史。本次未改写setup元数据以模拟首次，也未重复已经充分验证的重新接管。后续完整新安装首次分支结果另见主线汇总，不能归入本次批次。完整60分钟耗尽阈值另属未跑边界，不影响本次60分钟设置与短窗口增长的结论。", "", "恢复记录：", "", "```json", json.dumps({k: v for k, v in result.items() if k not in ("baseline", "usable_sample", "counting_phase")}, ensure_ascii=False, indent=2), "```", "", "原始端点、Result、0x44、操作ID、源码身份和输入哈希见 [next-day-evidence.json](next-day-evidence.json)。", "", "## 画面人工观察", ""]
    for name in ("next-day-60-usable", "next-day-bedtime-restored"):
        entry = visual_by_image.get("screenshots/"+name+".jpg")
        if entry and entry.get("status") == "human_confirmed":
            lines.append('- '+name+'：已人工确认；'+entry["observation"])
        else:
            lines.append('- '+name+'：待最终人工确认。')
    (out/"findings.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    body = '<!doctype html><meta charset="utf-8"><title>次日60分钟计时</title><style>body{font:16px system-ui;max-width:1100px;margin:24px;background:#f4f7fb;color:#182740}pre{white-space:pre-wrap;line-height:1.65}img{width:100%;max-width:900px}</style><p><a href="../index.html">主线报告</a> · <a href="next-day-evidence.json">精确读数与证据哈希</a></p><pre>'+html.escape("\n".join(lines))+'</pre>'
    for name in ("next-day-60-usable", "next-day-bedtime-restored"):
        path = "screenshots/"+name+".jpg"
        if (out/path).exists():
            body += '<h2>'+html.escape(name)+'</h2><img src="'+path+'">'
    (out/"index.html").write_text(body, encoding="utf-8")
    return evidence


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--host", default="192.168.0.106")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    r = report(args.out)["result"] if args.report_only else run(Device(args.host), args.out)
    print(json.dumps({k: r[k] for k in ("status", "error", "backend_restoration", "policy_restoration_error") if k in r}, ensure_ascii=False))
