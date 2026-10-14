#!/usr/bin/env python3
"""Fresh-install takeover reproduction with opaque old-directory preservation."""
from __future__ import annotations

import argparse
import ftplib
import hashlib
import hmac
import html
import json
from pathlib import Path
import re
import secrets
import time

from pctl_capture import EXEFS, REMOTE, restore, sha
from pctl_usage_scenarios import Mainline
from pctl_usage_evidence import transition
from usage_probe import Device, RELEASE, dump
from usage_probe_autostart import optional


def new_test_auth() -> dict:
    """Match Companion PIN encoding using an ephemeral test-only PIN.

    Original auth/credentials stay opaque in the archived directory. This
    prepares the normal preflight prerequisite without forging setup state.
    """
    pin = str(secrets.randbelow(10**12)).zfill(12).encode("ascii")
    salt = secrets.token_bytes(16)
    return {"version": 1, "pin_hash": hmac.new(pin, b"PTC-PIN1"+salt, hashlib.sha256).hexdigest(),
            "pin_salt": salt.hex(), "hash": "hmac-sha256", "updated_at": int(time.time()),
            "failed_attempts": 0, "cooldown_until": 0}


def post_grant_window(rows: list[dict], first: dict) -> dict:
    selected = [s for s in rows if s["boot_id"] == first["boot_id"] and s["day_index"] == first["day_index"]
                and int(s["tick_ns"]) >= int(first["tick_ns"]) and s.get("configured_minutes") == first["configured_minutes"]]
    selected.sort(key=lambda s: int(s["tick_ns"]))
    if len(selected) < 2:
        raise RuntimeError("no bounded post-grant growth window")
    last_tick = int(selected[-1]["tick_ns"])
    selected = sorted([s for s in rows if s["boot_id"] == first["boot_id"] and s["day_index"] == first["day_index"]
                       and int(first["tick_ns"]) <= int(s["tick_ns"]) <= last_tick], key=lambda s: int(s["tick_ns"]))
    for s in selected:
        if s.get("source_hash") != first.get("source_hash") or s.get("run_id") != first.get("run_id") or s.get("configured_minutes") != first["configured_minutes"] or any(s.get(k) != 0 for k in ("sample_error", "spent_result", "remaining_result", "settings_result", "timer_result", "restricted_result")) or not s["timer_enabled"] or s["restricted"] or not s.get("configured_available") or int(s["spent_ns"])+int(s["remaining_ns"]) != s["configured_minutes"]*60*10**9:
            raise RuntimeError("post-grant window contains invalid or unusable read")
    for a, b in zip(selected, selected[1:]):
        if not 0 < int(b["tick_ns"])-int(a["tick_ns"]) <= 10*10**9 or int(b["spent_ns"]) < int(a["spent_ns"]):
            raise RuntimeError("post-grant window has gap or counter decrease")
    c = transition(selected[0], selected[-1])
    if c["elapsed_s"] < 60 or abs(c["spent_ns_delta_s"]-c["elapsed_s"]) > 2:
        raise RuntimeError("post-grant raw window does not prove at least 60 seconds of growth")
    return {"before": selected[0], "after": selected[-1], "comparison": c, "sample_count": len(selected),
            "scope": "raw contiguous usable samples through the last sample before original policy restoration; original host phase endpoints retained"}


def roots(plan: dict) -> tuple[str, str, str]:
    rid = plan["run_id"]
    if not re.fullmatch(r"[0-9a-f]{32}", rid):
        raise RuntimeError("invalid directory transaction identity")
    expected = (RELEASE, RELEASE+"-before-first-"+rid, RELEASE+"-first-test-"+rid)
    if tuple(plan[k] for k in ("original_root", "old_root_archive", "test_root_archive")) != expected:
        raise RuntimeError("directory targets differ from explicitly scoped transaction")
    return expected


def directory_exists(d: Device, path: str) -> bool:
    with d.ftp() as f:
        try:
            f.cwd(path)
            return True
        except ftplib.error_perm as exc:
            if str(exc).startswith("550"):
                return False
            raise


def require_before_enable(sample: dict, setup: dict, state: dict, day: int) -> None:
    if sample["stage"] != "before_standard_bootstrap" or sample["day_index"] != day:
        raise RuntimeError("missing same-day capture before standard bootstrap")
    if any(sample.get(k) != 0 for k in ("sample_error", "spent_result", "remaining_result", "settings_result", "timer_result", "restricted_result")):
        raise RuntimeError("failed raw baseline read")
    if int(sample["spent_ns"]) <= 3600*10**9 or sample.get("settings_mode") != "unconfigured":
        raise RuntimeError("actual native usage not over 60 minutes or numeric quota present")
    if setup["phase"] != "unconfigured" or setup.get("snapshot_available") or setup.get("handover_today_pending"):
        raise RuntimeError("not the newly generated first setup state")
    if not state["restriction_enabled"] or state["temporary_unlocked"] or state["restricted_now"] != 0:
        raise RuntimeError("parental control baseline invalid")


def isolate(d: Device, out: Path) -> dict:
    plan = json.loads((out/"plan.json").read_text(encoding="utf-8"))
    root, old, test = roots(plan)
    task = Mainline(d, out)
    if not (out/"native-preuse-window-ended.json").exists() or time.time() < plan["native_preuse_target_host_unix_s"]:
        raise RuntimeError("real pre-use observation window not finished")
    if (out/"directory-transaction.json").exists():
        raise RuntimeError("existing directory transaction; inspect rather than replay")
    state = task.request("status")["state"]
    setup = d.json(root+"/setup.json")
    if state["day_index"] != plan["day_index"] or not state["restriction_enabled"] or state["restricted_now"] or state["play_timer_enabled"] != 1 or state["limited_today"] or state["blocked_today"] or setup["phase"] != "restored":
        raise RuntimeError("native no-quota baseline no longer usable")
    if optional(d, root+"/flags/disable.flag") != b"install_snapshot_restored\n":
        raise RuntimeError("old controller not disabled by our acknowledged restore")
    if directory_exists(d, old) or directory_exists(d, test):
        raise RuntimeError("archive already exists; never overwrite it")
    hashes = json.loads((out/"template-hashes.json").read_text(encoding="utf-8"))
    for name, digest in hashes.items():
        if sha((out/"installation-templates"/name).read_bytes()) != digest:
            raise RuntimeError("installation template changed")
    template = json.loads((out/"installation-templates/setup.json").read_text(encoding="utf-8"))
    if template["phase"] != "unconfigured" or template["snapshot_available"]:
        raise RuntimeError("template is not the original fresh-install default")
    if sha(d.read(EXEFS)) != task.install["capture_sha256"]:
        raise RuntimeError("prepared capture image changed")
    task.picture("native-preuse-end")
    tx = {"phase": "old_directory_rename_pending", "root": root, "old": old, "test": test, "run_id": plan["run_id"], "no_live_setup_field_written": True}
    dump(out/"directory-transaction.json", tx)
    with d.ftp() as f:
        f.rename(root, old)
    if not directory_exists(d, old):
        raise RuntimeError("old directory rename not confirmed")
    tx["phase"] = "old_directory_archived"
    dump(out/"directory-transaction.json", tx)
    # Install only immutable defaults, not any live config/auth/setup file.
    for name, digest in hashes.items():
        data = (out/"installation-templates"/name).read_bytes()
        d.write(root+"/defaults/"+name, data)
        if sha(d.read(root+"/defaults/"+name)) != digest:
            raise RuntimeError("fresh template upload readback mismatch")
    d.write(root+"/build.json", (out/"original-build.json").read_bytes())
    if optional(d, root+"/setup.json") is not None or optional(d, root+"/backups/install_pctl_snapshot.json") is not None:
        raise RuntimeError("live setup unexpectedly exists before fresh normal boot")
    if optional(d, root+"/auth.json") is not None:
        raise RuntimeError("unexpected live auth before test PIN preparation")
    auth = new_test_auth()
    d.put_json(root+"/auth.json", auth)
    if d.json(root+"/auth.json") != auth:
        raise RuntimeError("test PIN preparation readback failed")
    tx["test_pin_prepared"] = True
    tx["original_auth_read"] = False
    tx["phase"] = "fresh_install_ready_waiting_normal_boot"
    tx["prepared_host_unix_s"] = int(time.time())
    dump(out/"directory-transaction.json", tx)
    return tx


def restore_directory(d: Device, out: Path) -> dict:
    plan = json.loads((out/"plan.json").read_text(encoding="utf-8"))
    root, old, test = roots(plan)
    tx_path = out/"directory-transaction.json"
    tx = json.loads(tx_path.read_text(encoding="utf-8")) if tx_path.exists() else {"phase": "no_directory_isolation"}
    if directory_exists(d, old):
        if directory_exists(d, test):
            raise RuntimeError("test archive exists; inspect partial restore, no overwrite")
        tx["phase"] = "test_directory_archive_pending"
        runtime = optional(d, root+"/handover/runtime-ready.json")
        if runtime is not None:
            # The running process generated this public identity at its fresh
            # boot; retain it when restoring its data directory, not the old PID.
            (out/"fresh-runtime-before-directory-restore.json").write_bytes(runtime)
            tx["current_generated_runtime_sha256"] = sha(runtime)
        dump(tx_path, tx)
        if directory_exists(d, root):
            with d.ftp() as f:
                f.rename(root, test)
        tx["phase"] = "old_directory_restore_pending"
        dump(tx_path, tx)
        with d.ftp() as f:
            f.rename(old, root)
    elif tx.get("phase") not in ("no_directory_isolation", "original_directory_and_policy_restored"):
        raise RuntimeError("old archive absent in unresolved transaction; manual inspection required")
    pre = json.loads((out/"policy-preimages.json").read_text(encoding="utf-8"))
    if optional(d, root+"/flags/disable.flag") != b"install_snapshot_restored\n":
        raise RuntimeError("original paused directory marker differs; do not blindly resume it")
    for name, entry in pre.items():
        if name == "flags/disable.flag":
            continue
        path = root+"/"+name
        if entry["exists"]:
            data = (out/"public-preimages"/name).read_bytes()
            if sha(data) != entry["sha256"]:
                raise RuntimeError("saved public policy preimage changed")
            d.write(path, data)
            if sha(d.read(path)) != entry["sha256"]:
                raise RuntimeError("original public policy readback failed")
        elif optional(d, path) is not None:
            with d.ftp() as f:
                f.delete(path)
    runtime_file = out/"fresh-runtime-before-directory-restore.json"
    if runtime_file.exists():
        runtime = runtime_file.read_bytes()
        if sha(runtime) != tx["current_generated_runtime_sha256"]:
            raise RuntimeError("captured current process identity changed")
        d.write(root+"/handover/runtime-ready.json", runtime)
    d.write(root+"/flags/reload.flag", b"fresh-test-original-policy-restored\n")
    # Last step resumes the original active policy after its complete preimage.
    with d.ftp() as f:
        f.delete(root+"/flags/disable.flag")
    tx["phase"] = "original_directory_and_policy_restored"
    dump(tx_path, tx)
    return tx


def finish(d: Device, out: Path) -> dict:
    task = Mainline(d, out)
    plan = json.loads((out/"plan.json").read_text(encoding="utf-8"))
    tx = json.loads((out/"directory-transaction.json").read_text(encoding="utf-8"))
    if tx["phase"] != "fresh_install_ready_waiting_normal_boot" or not tx.get("test_pin_prepared"):
        raise RuntimeError("new-installation boot not armed")
    result: dict = {"status": "running", "original_historical_incident_reproduced": False,
                   "scope": "reproducible new-installation first-setup branch", "no_date_write": True}
    try:
        now = task.sample()
        rows = json.loads((out/"normalized-samples.json").read_text(encoding="utf-8"))
        baseline = next(s for s in rows if s["boot_id"] == now["boot_id"] and s["stage"] == "before_standard_bootstrap")
        setup = d.json(RELEASE+"/setup.json")
        dump(out/"fresh-setup-before-enable.json", setup)
        state = task.request("status")["state"]
        require_before_enable(baseline, setup, state, plan["day_index"])
        if now["boot_id"] == task.install["runtime_before"]["boot_id"]:
            raise RuntimeError("old process still running")
        result["actual_first_before_bootstrap"] = baseline
        task.picture("fresh-before-first-enable")
        action = task.action("complete_setup")
        result["first_complete_setup"] = action
        deadline = time.monotonic()+25
        while time.monotonic() < deadline:
            after_setup = d.json(RELEASE+"/setup.json")
            if after_setup["phase"] == "active":
                break
            time.sleep(1)
        else:
            raise RuntimeError("first setup did not activate within its normal grace")
        dump(out/"fresh-setup-after-enable.json", after_setup)
        dump(out/"fresh-active-status.json", task.request("status"))
        settled = task.phase("fresh-first-policy-observation", 10)
        if int(settled["after"]["spent_ns"]) < int(baseline["spent_ns"]):
            raise RuntimeError("history lost when first policy became active")
        result["first_policy_observation"] = settled
        task.picture("fresh-after-first-enable")
        # A fresh weekly 60-minute default can legitimately restrict a >60min
        # native history. Keep that first result; only then grant usability for
        # the independent post-takeover counting window.
        if settled["after"]["restricted"]:
            if settled["after"].get("configured_minutes") != 60:
                raise RuntimeError("unexpected first restriction; no blind grant")
            granted = task.action("add_today_minutes", {"minutes": 120})
            if granted["after"]["restricted"] or granted["comparison"]["counter_decreased"] or granted["comparison"]["excessive_jump"]:
                raise RuntimeError("explicit grant failed or cumulative jumped")
            result["explicit_after_first_grant"] = granted
            task.picture("fresh-after-grant-usable")
        growth = task.phase("fresh-first-takeover-growth", 60)
        c = growth["comparison"]
        if not c["same_boot"] or c["counter_decreased"] or abs(c["spent_ns_delta_s"]-c["elapsed_s"]) > 2 or int(growth["before"]["spent_ns"]) < int(baseline["spent_ns"]) or growth["after"]["restricted"] or not growth["before"]["timer_enabled"] or not growth["after"]["timer_enabled"]:
            raise RuntimeError("cumulative history retention or post-takeover growth not proved")
        result.update(status="passed_reproducible_fresh_installation_first_takeover", growth=growth, setup_active=True)
    except Exception as exc:
        result.update(status="incomplete", error=type(exc).__name__+": "+str(exc))
    finally:
        ambiguous = [op["request"]["request_id"] for op in task.operations[task.operation_start:] if op["request"]["type"] != "status" and op["outcome"] in ("pending", "unknown_no_replay")]
        if ambiguous:
            result["unknown_mutating_request_no_replay"] = ambiguous
            result["directory_restoration_pending"] = True
        else:
            try:
                result["directory_restoration"] = restore_directory(d, out)["phase"]
                time.sleep(7)
                state = task.request("status")
                dump(out/"restored-original-status.json", state)
                task.picture("original-policy-restored")
                if state["setup"]["phase"] != "active" or state["state"]["restricted_now"] != 1 or state["state"]["bedtime"]["skipped"]:
                    raise RuntimeError("original active bedtime policy not confirmed")
            except Exception as exc:
                result["directory_restoration_error"] = type(exc).__name__+": "+str(exc)
        try:
            result["backend_restoration"] = restore(d, out, json.loads((out/"capture-install.json").read_text(encoding="utf-8")))["phase"]
        except Exception as exc:
            result["backend_restoration_error"] = type(exc).__name__+": "+str(exc)
        dump(out/"fresh-first-result.json", result)
    return result


def report(out: Path) -> dict:
    result = json.loads((out/"fresh-first-result.json").read_text(encoding="utf-8"))
    journal = json.loads((out/"mainline-run.json").read_text(encoding="utf-8"))
    visual_path = out/"manual-visual-review.json"
    visual = json.loads(visual_path.read_text(encoding="utf-8")) if visual_path.exists() else {"observations": []}
    visual_by_image = {entry["image"]: entry for entry in visual["observations"]}
    names = ["plan.json", "capture-install.json", "directory-transaction.json", "fresh-first-result.json", "mainline-run.json", "normalized-samples.json", "collection.json", "native-preuse-observations.jsonl", "native-preuse-window-ended.json", "prior-preuse-provenance.json", "fresh-setup-before-enable.json", "fresh-setup-after-enable.json", "restored-original-status.json", "final-audit.json", "validation.json"]
    names.append("manual-visual-review.json")
    evidence = {"result": result, "operations_and_phases": journal, "manual_visual_review": visual,
                "input_files_sha256": {n: hashlib.sha256((out/n).read_bytes()).hexdigest() for n in names if (out/n).exists()}}
    if (out/"final-audit.json").exists():
        evidence["final_audit"] = json.loads((out/"final-audit.json").read_text(encoding="utf-8"))
    dump(out/"fresh-first-evidence.json", evidence)
    lines = ["# 完整新安装首次接管复现", "", "范围：已有原生家长控制、无具体额度、真实此前累计>60分钟；完整新PlayWise数据目录的首次设置分支。不追溯原始历史反馈现场。", "", "取证状态："+result["status"], ""]
    a = result.get("actual_first_before_bootstrap")
    if a:
        lines += [f'- 首个1952位于标准bootstrap之前，累计{int(a["spent_ns"])/1e9:g}秒；当天槽位={a["settings_mode"]}，本地日期day_index={a["day_index"]}。', '- 默认模板由正常启动生成现场，未手写live setup字段。旧应用数据整体隔离，旧凭据未读取或展示。', ""]
    p = result.get("first_policy_observation")
    if p:
        b = p["after"]
        lines += [f'- 首次启用后累计{int(b["spent_ns"])/1e9:g}秒，实际额度{b.get("configured_minutes")}分钟，restricted={b["restricted"]}，带符号剩余{int(b["remaining_ns"])/1e9:g}秒。受限时无需继续累计。', ""]
    granted = result.get("explicit_after_first_grant")
    if granted:
        b = granted["after"]
        lines += [f'- 初次设置后的显式+120将额度调整为{b.get("configured_minutes")}分钟，累计{int(b["spent_ns"])/1e9:g}秒；恢复可用后再验证增长。加时属于首接管记录之后的独立操作。', ""]
    if result.get("growth"):
        c = result["growth"]["comparison"]
        lines += [f'- 后续同启动窗口{c["elapsed_s"]:.2f}秒，累计增加{c["spent_ns_delta_s"]:g}秒。', ""]
    if evidence.get("final_audit", {}).get("post_grant_raw_window"):
        c = evidence["final_audit"]["post_grant_raw_window"]["comparison"]
        lines += [f'- 完整原始记录中的连续可用窗口{c["elapsed_s"]:.2f}秒，累计增加{c["spent_ns_delta_s"]:g}秒，覆盖至少60秒。宿主phase请求60秒，但选中的周期采样端点只有57.35秒；原phase不回写，恢复前追加的原始样本单独补齐窗口。后续phase选择已要求设备tick本身跨过目标时长。', ""]
    lines += ["恢复结果及限制：", "", "```json", json.dumps({k: v for k, v in result.items() if k not in ("actual_first_before_bootstrap", "first_complete_setup", "first_policy_observation", "explicit_after_first_grant", "growth")}, ensure_ascii=False, indent=2), "```", "", "原始读数、操作ID、目录事务和输入哈希见 [fresh-first-evidence.json](fresh-first-evidence.json)。本次未实施产品算法修改。", "", "## 画面人工观察", ""]
    images = ["native-preuse-start", "native-preuse-end", "fresh-before-first-enable", "fresh-after-first-enable", "fresh-after-grant-usable", "original-policy-restored"]
    for name in images:
        if (out/"screenshots"/(name+".jpg")).exists():
            entry = visual_by_image.get("screenshots/"+name+".jpg")
            if entry:
                lines.append('- '+name+'：已人工核对；'+entry["observation"])
            else:
                lines.append('- '+name+'：待人工确认。')
    if any(entry.get("status") == "human_reviewed_transition" for entry in visual["observations"]):
        lines += ["", "加120截图仅确认官方限制提示半透明、疑似解除过渡；没有视觉确认已完全解除、HOME已显示或无额度。独立机器证据为实际额度180分钟、restricted=false，随后62.60秒累计增加62秒；不把机器结果改写为截图已确认。"]
    (out/"findings.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    body = '<!doctype html><meta charset="utf-8"><title>新安装首次接管证据</title><style>body{font:16px system-ui;max-width:1100px;margin:24px;background:#f4f7fb;color:#182740}pre{white-space:pre-wrap;line-height:1.65}img{width:100%;max-width:900px}</style><p><a href="../index.html">主线报告</a> · <a href="fresh-first-evidence.json">精确端点与哈希</a></p><pre>'+html.escape("\n".join(lines))+'</pre>'
    for name in images:
        if (out/"screenshots"/(name+".jpg")).exists():
            entry = visual_by_image.get("screenshots/"+name+".jpg")
            state = "已人工核对（解除过渡）" if entry and entry.get("status") == "human_reviewed_transition" else "已人工确认" if entry else "待人工确认"
            body += '<h2>'+html.escape(name)+'，'+state+'</h2>'
            if entry:
                body += '<p>'+html.escape(entry["observation"])+'</p>'
            body += '<img src="screenshots/'+name+'.jpg">'
    (out/"index.html").write_text(body, encoding="utf-8")
    return evidence


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("isolate", "finish", "restore-directory", "report"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--host", default="192.168.0.106")
    args = parser.parse_args()
    result = report(args.out)["result"] if args.action == "report" else {"isolate": isolate, "finish": finish, "restore-directory": restore_directory}[args.action](Device(args.host), args.out)
    print(json.dumps({k: result[k] for k in ("phase", "status", "error", "directory_restoration", "directory_restoration_error", "backend_restoration") if k in result}, ensure_ascii=False))
