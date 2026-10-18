#!/usr/bin/env python3
"""Merge bounded new PCTL evidence without upgrading partial lifecycle claims."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import html
import json

from tools.legacy.pctl_usage_evidence import transition, valid_spent
from tools.legacy.usage_probe import dump


def lifecycle_candidates(rows: list[dict]) -> dict:
    """Numerical candidates only: network/uptime evidence is not a sleep attestation."""
    groups: dict[str, list[dict]] = {}
    for row in rows:
        if valid_spent(row) is not None:
            groups.setdefault(row["boot_id"], []).append(row)
    sessions = sorted(groups.values(), key=lambda g: min(int(s["unix_s"]) for s in g))
    gaps, reboots, awake = [], [], []
    for group in sessions:
        group.sort(key=lambda s: int(s["tick_ns"]))
        for a, b in zip(group, group[1:]):
            wall = int(b["unix_s"]) - int(a["unix_s"])
            change = transition(a, b)
            if wall >= 90:
                gaps.append({"before": a, "after": b, "wall_gap_s": wall,
                             "comparison": change, "manual_sleep_confirmation_required": True,
                             "counter_frozen_candidate": a["day_index"] == b["day_index"]
                             and not a.get("restricted") and not b.get("restricted")
                             and 0 <= int(b["spent_ns"])-int(a["spent_ns"]) <= 15_000_000_000})
        # A counting window after each long gap is independently evaluated.
        starts = [group[0]] + [g["after"] for g in gaps if g["after"]["boot_id"] == group[0]["boot_id"]]
        for a in starts:
            eligible = [s for s in group if int(s["tick_ns"])-int(a["tick_ns"]) >= 60_000_000_000]
            if eligible:
                b = eligible[0]
                awake.append({"before": a, "after": b, "comparison": transition(a, b)})
    for older, newer in zip(sessions, sessions[1:]):
        a, b = older[-1], newer[0]
        wall = int(b["unix_s"])-int(a["unix_s"])
        delta = int(b["spent_ns"])-int(a["spent_ns"])
        reboots.append({"before": a, "after": b, "wall_gap_s": wall,
                        "direct_delta_s": delta/1e9,
                        "bounded_retention_candidate": a["day_index"] == b["day_index"]
                        and 0 <= wall <= 180 and 0 <= delta <= (wall+2)*1e9
                        and int(b["tick_ns"]) < int(a["tick_ns"])
                        and a["source_hash"] == b["source_hash"]
                        and b["stage"] == "before_standard_bootstrap",
                        "manual_normal_reboot_confirmation_required": True})
    return {"sleep_gaps": gaps, "reboot_pairs": reboots, "awake_windows": awake,
            "status": "numerical_candidates_require_manual_operation_record"}


def report(root: Path) -> dict:
    out, live = root / "pctl-mainline", root / "pctl-mainline/live-capture"
    inputs: dict[str, str] = {}
    def read(relative: str) -> dict:
        path = out / relative
        inputs[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        return json.loads(path.read_text(encoding="utf-8"))
    old = read("evidence.json")
    chain = read("live-capture/recovered-chain.json")
    retakeover = read("live-capture/retakeover/mainline-run.json")
    preconditions = read("live-capture/retakeover/preconditions.json")
    lifecycle_path = live / "lifecycle/normalized-samples.json"
    lifecycle = lifecycle_candidates(read("live-capture/lifecycle/normalized-samples.json")) if lifecycle_path.exists() else {"status": "pending"}
    final = {"old_scoped_evidence": old, "new_grant_chain": chain,
             "constructed_retakeover": {"status": "verified_constructed_retakeover_only", "preconditions": preconditions, "run": retakeover},
             "true_first_takeover": {"status": "pending", "reason": "fresh installation first-setup retention chain not yet complete; original historical incident cannot be reconstructed"},
             "lifecycle_candidates": lifecycle, "input_hashes": inputs,
             "product_algorithm_changed": False}
    manual = live / "lifecycle/manual-verdict.json"
    if manual.exists():
        final["lifecycle_verdict"] = read("live-capture/lifecycle/manual-verdict.json")
    visual_path = out / "manual-visual-review.json"
    visual = read("manual-visual-review.json") if visual_path.exists() else {"observations": []}
    visual_by_image = {entry["image"]: entry for entry in visual["observations"]}
    final["manual_visual_review"] = visual
    dump(out / "final-evidence.json", final)
    lines = ["# PCTL 1952 主线实测汇总", "", "使用口径：游戏、HOME、系统设置正常可用期间计时；限制使机器无法正常使用时允许冻结。本轮只验证能力，不修改产品显示算法。", "",
             "## 已充分验证", "", "旧原始证据中的游戏、HOME、系统设置约1:1增长；画面已由用户核对，无需重复实验。1770条可信限时样本全部满足：额度纳秒 − 带符号1454剩余 = 1952累计。", "",
             "本轮标准同进程旁路补齐323→443→563分钟的连续两次+120链路。323和443阶段累计均30133秒；563首次观察累计30135秒、剩余+3645秒，恢复可用。前两段冻结符合受限口径，解除后62.19591073秒增加62秒。加时没有增加7200秒累计。原状态接口已用分钟323→443→503暴露了负剩余被截断后的显示跳涨。", "",
             "以上链路宿主汇总曾被补充状态查询覆盖；已经用未变更的设备原始采样、原始请求/回执和原记录端点时长重建。重建端点与相邻额度变化样本分别标记，不伪称后者为原动作选择端点。六个原始请求均有成功回执，规则与就寝限制随后恢复。", "",
             "## 三个关键场景", "", "- 休眠／唤醒：待集中实体操作确认。下面数值候选不能单凭断网或采样中断判为真休眠。", "- 同日重启：待集中实体操作和有界原始端点配对。先前故障重启及大累计只支持保留，不能当完整通过。", "- 首次接管：真正首次未验证；构造重新接管已验证。恢复原生安装前像后，家长控制开启、当天无具体额度、PlayWise控制停用时直接读取30202秒；60.295562864秒增加60秒，重新启用后累计保留，后续15.608836927秒增加15秒。这不能替代同日从未接管过的现场。", "",
             "## 算法建议与剩余事项", "", "1952支持作为该使用口径下的直接显示来源，已足以定位原显示算法的问题；是否满足完整生命周期验收仍按休眠、重启和真正首次各自证据判断。实现建议另列：保留累计原值，错误时显示未知，可信额度下用带符号1454交叉验证；日切、缓存及服务资格另测。", "",
             "PDM历史、应用Active Time、纯游戏前台、多账号、改日期及其他跨日边界、PIN、底座切换保留后续，不作为当前主线完成条件。已测自然午夜的结果单列，不外推为所有跨日情形。首次全零初始化亦不冒充当前已启用家长控制的口径。", "",
             "## 数值候选（仅取证，不自动判通过）", ""]
    for g in lifecycle.get("sleep_gaps", []):
        lines.append(f'- 同会话采样间隔{g["wall_gap_s"]}秒，累计增加{g["comparison"].get("spent_ns_delta_s")}秒；需实体操作记录确认。')
    for g in lifecycle.get("reboot_pairs", []):
        lines.append(f'- 启动会话切换间隔{g["wall_gap_s"]}秒，累计增加{g["direct_delta_s"]}秒；有界保留候选={g["bounded_retention_candidate"]}。')
    if "lifecycle_verdict" in final:
        verdict = final["lifecycle_verdict"]
        sleep = verdict["sleep_wake"]
        boot = verdict["same_day_reboot"]
        gap = sleep["sleep_gap"]
        wake = sleep["awake_after_wake"]["comparison"]
        pair = boot["pair"]
        growth = boot.get("post_reboot_growth", {}).get("comparison", {})
        for index, line in enumerate(lines):
            if line.startswith("- 休眠／唤醒："):
                lines[index] = f'- 休眠／唤醒：本次真休眠与恢复增长通过。用户确认整组实体操作完成；同会话{gap["comparison"]["elapsed_s"]:.2f}秒睡眠采样间隔累计+{gap["comparison"]["spent_ns_delta_s"]:.0f}秒，唤醒后{wake["elapsed_s"]:.2f}秒累计+{wake["spent_ns_delta_s"]:.0f}秒。宿主期间仅拉取日志，无status/控制请求。具体按钮时间未逐次记录，保留边界限制。'
            elif line.startswith("- 同日重启："):
                lines[index] = f'- 同日重启：同日累计保留与恢复增长通过。有界端点间隔{pair["wall_gap_s"]}秒，累计{int(pair["before"]["spent_ns"])/1e9:.0f}→{int(pair["after"]["spent_ns"])/1e9:.0f}秒；新启动首次读取位于标准bootstrap之前，单调uptime回退，同一天且源码身份一致。随后{growth.get("elapsed_s", 0):.2f}秒累计+{growth.get("spent_ns_delta_s", 0):.0f}秒。第一条timer仍关闭，约{boot["time_from_first_capture_to_first_on_s"]:.2f}秒后首次启用；未宣称启动画面出现时立即计时。此前629秒过期间隔配对排除，不能冒充有界重启实验。'
        lines += ["", "完整生命周期端点与判断见 [manual-verdict.json](live-capture/lifecycle/manual-verdict.json)，实体确认见 [manual-attestation.json](live-capture/lifecycle/manual-attestation.json)。"]
    restoration = live / "lifecycle/final-restoration.json"
    if restoration.exists():
        restored = read("live-capture/lifecycle/final-restoration.json")
        final["restoration"] = restored
        lines += ["", "恢复状态：原周计划/今日规则一致，本次就寝跳过已清除，限制恢复。原后台NSP已按哈希恢复到SD，采集配置归档，内存中同进程后台的旁路已停用；下次正常开机加载原二进制，无需为结束采集再次值守重启。"]
    if (live / "lifecycle/final-audit.json").exists():
        audit = read("live-capture/lifecycle/final-audit.json")
        final["final_audit"] = audit
        lines += ["", f'采集审计：{audit["all_capture_rows"]}条原始记录、{audit["sessions"]}个启动会话；服务错误{audit["service_error_rows"]}条，同会话累计倒退{len(audit["same_boot_decreases"])}次。停采后6秒原始文件保持不变；标准启动项保留，旧独立探针启动项停用。']
    if (out / "validation.json").exists():
        final["validation"] = read("validation.json")
        lines += ["", "验证：`python -X utf8 tests/devkit/test_pctl_usage_evidence.py` 18项通过；设置子进程UTF-8后 `python -X utf8 tools/test.py` 全量通过；`python -X utf8 tools/package_remote.py --with-pctl-capture --port 18881` 通过标准包、内部惰性包和默认Eden NRO校验，包含7个旁路C用例；`git diff --check` 通过。主机回归不替代上述实际设备条件。"]
    if (out / "cross-midnight/capture-install.json").exists():
        supplement = read("cross-midnight/capture-install.json")
        final["optional_midnight_supplement"] = {"run_id": supplement["run_id"], "phase": supplement["phase"], "report": "cross-midnight/index.html"}
        if supplement["phase"] == "original_restored_on_disk_tap_disabled":
            lines += ["", "可选自然跨天补测已收尾，详见 [跨天独立报告](cross-midnight/index.html)。原主线结果保持；补测原像恢复另外记账。"]
            if (out / "cross-midnight/midnight-evidence.json").exists():
                midnight = read("cross-midnight/midnight-evidence.json")
                final["optional_midnight_supplement"]["evidence"] = midnight
                lines += ["", "本次自然午夜同启动日期2477→2478，1952累计31057→0秒、带符号1454剩余−31057→0秒；归零在23:59:57至00:00:02的5秒区间内，203条读数无读取错误或交叉不一致。次日周计划60分钟与此前预测一致，整夜就寝0分钟覆盖保持。次日解除就寝后正常增长及60分钟实际可用执行仍待补；新增两张画面的人工观察见跨天报告。"]
        else:
            lines += ["", "最新设备状态：正在追加可选自然跨天采集，同进程记录器重新加载，原后台备份保留；最终恢复延后至00:04左右。本节上面的恢复证明属于先前主线结束时的历史状态。补测见 [执行计划](cross-midnight/plan.md)，不把采集中写为已完成。"]
    if (out / "next-day-growth/next-day-evidence.json").exists():
        resumed = read("next-day-growth/next-day-evidence.json")
        final["next_day_resume"] = resumed
        r = resumed["result"]
        if r["status"] == "passed_actual_base_quota_and_next_day_growth":
            c = r["counting_phase"]["comparison"]
            lines += ["", f'次日可用窗口已补齐：实际PCTL额度60分钟，{c["elapsed_s"]:.2f}秒内1952增加{c["spent_ns_delta_s"]:g}秒、1454剩余减少{-c["remaining_ns_delta_s"]:g}秒，带符号关系吻合。恢复就寝后再次冻结，规则与原后台文件恢复。详见 [次日计时报告](next-day-growth/index.html)。这验证60分钟设置实际应用及短窗口增长，不代表完整60分钟耗尽测试。']
            lines = [line.replace("次日解除就寝后正常增长及60分钟实际可用执行仍待补", "该跨午夜窗口当时未验证次日可用增长，后续已由独立次日批次补齐60分钟设置应用与短窗口增长") for line in lines]
    if (out / "first-fresh-install-plan/plan.json").exists():
        final["first_fresh_install_plan"] = read("first-fresh-install-plan/plan.json")
        lines += ["", "完整新安装首次分支的范围见 [复现计划](first-fresh-install-plan/plan.md)。旧PlayWise数据整体隔离，由安装默认模板生成新状态，区别于手改setup或此前restored重新启用。实际执行结果以下述独立批次为准。"]
    if (out / "first-fresh-install/plan.json").exists():
        first_plan = read("first-fresh-install/plan.json")
        final["fresh_installation_first_test"] = {"plan": first_plan, "status": "native_preuse_in_progress"}
        if (out / "first-fresh-install/fresh-first-result.json").exists():
            fresh = read("first-fresh-install/fresh-first-result.json")
            final["fresh_installation_first_test"]["result"] = fresh
            final["fresh_installation_first_test"]["status"] = fresh["status"]
            baseline = fresh.get("actual_first_before_bootstrap")
            if baseline:
                final["true_first_takeover"] = {"status": "partial_pre_takeover_history_read_verified", "baseline": baseline,
                    "reason": "pre-bootstrap cumulative read proved; complete_setup refused due to absent test PIN, so first takeover retention remains pending",
                    "original_historical_incident_reproduced": False}
                lines += ["", f'新安装首轮的接管前读取通过：标准bootstrap前1952={int(baseline["spent_ns"])/1e9:g}秒，原生当天未配置额度且家长控制开启、未受限。首次启用因测试PIN未准备返回308 setup_pending，未进入接管写入；累计可读不等于接管保留链路完整通过。原目录、规则及后台恢复后单独准备修正前置条件的重试。失败证据见 [新安装首轮报告](first-fresh-install/index.html)。']
        else:
            lines += ["", "最新设备状态：新安装首次接管复现的真实无额度预使用阶段正在执行；原PlayWise控制暂时停用，原生家长控制开启且机器可用。上面的恢复证明属于此前批次结束时；此批次将于取证完成后单独恢复。累计>60分钟前提待最后重启的原始1952核验，未提前标通过。"]
    retry_root = out / "first-fresh-install-retry"
    if (retry_root / "plan.json").exists():
        retry = {"plan": read("first-fresh-install-retry/plan.json"), "status": "awaiting_normal_reboot"}
        final["fresh_installation_first_retry"] = retry
        if (retry_root / "fresh-first-result.json").exists():
            r = read("first-fresh-install-retry/fresh-first-result.json")
            retry.update(result=r, status=r["status"])
            audit = read("first-fresh-install-retry/final-audit.json") if (retry_root/"final-audit.json").exists() else {}
            retry["audit"] = audit
            if r["status"] == "passed_reproducible_fresh_installation_first_takeover" and audit.get("post_grant_raw_window"):
                a = r["actual_first_before_bootstrap"]
                b = r["first_policy_observation"]["after"]
                c = audit["post_grant_raw_window"]["comparison"]
                final["true_first_takeover"] = {"status": "verified_reproducible_fresh_installation_first_takeover", "result": r,
                    "original_historical_incident_reproduced": False, "scope": "new PlayWise installation on existing parental-control console; not virgin hardware"}
                final["mainline_capability_status"] = "verified_accepted_usage_scope_new_visuals_pending"
                lines = [f'- 首次接管：完整新安装首次分支复现通过。原生家长控制开启、无额度、标准bootstrap前累计{int(a["spent_ns"])/1e9:g}秒；安装默认生成unconfigured，无旧live setup/快照。首次启用后累计{int(b["spent_ns"])/1e9:g}秒，后续{c["elapsed_s"]:.2f}秒增加{c["spent_ns_delta_s"]:g}秒。旧数据整体隔离，测试PIN独立生成；不追溯最初反馈现场。' if line.startswith('- 首次接管：') else line for line in lines]
                lines += ["", "完整首次分支端点、默认规则实际执行、必要加时与恢复见 [新安装重试完整报告](first-fresh-install-retry/index.html)。首轮308失败保持原样；成功重试单独记账。"]
                lines += ["", "按当前接受口径，1952直接累计已具备游戏/HOME/设置、受限冻结、休眠/唤醒、同日正常重启和完整新安装首次接管的能力证据，支持修正原已用显示算法。范围限于本机、本固件及已测条件，不外推原历史现场或所有边界；新增画面尚待统一人工确认。", "", f'最新收尾：原目录与规则恢复、就寝恢复受限，原后台SD映像按SHA恢复且采集停止。新安装重试{audit["rows"]}条原始记录无服务错误、无同日倒退；{audit["trusted_quota_rows"]}条可信额度读数与带符号剩余一致。完整可用62.60秒窗口以恢复前连续原始记录补齐，原宿主phase的57.35秒端点不回写。']
                lines = [line.replace("是否满足完整生命周期验收仍按休眠、重启和真正首次各自证据判断", "休眠、同日重启和完整新安装首次接管已有各自原始证据，原历史反馈现场仍不可追溯") for line in lines]
                if (retry_root/"validation.json").exists():
                    final["latest_validation"] = read("first-fresh-install-retry/validation.json")
                    lines += ["", "最新验证：26项专项、全量UTF-8 Python回归及git diff --check通过；无新C修改，沿用前述统一构建通过的标准包、内部包和默认Eden NRO校验。"]
            else:
                lines += ["", "新安装重试尚未完整通过，实际失败原因及恢复状态见 [独立结果](first-fresh-install-retry/fresh-first-result.json)。保留已证明的接管前累计读取，不能据此自动升级首次接管保留结论。"]
        else:
            lines += ["", "最新设备状态：完整新安装重试已准备，测试PIN满足首次预检，原PIN/密钥保留在隔离目录且未读取；等待正常重启加载新的记录器。此前恢复说明属于历史批次，本批次尚未收尾。无需重复61分钟预使用，仍须新批次启动前1952实际>3600秒。"]
    review_path = out/"manual-final-visual-review.json"
    if review_path.exists():
        reviewed = read("manual-final-visual-review.json")
        final["final_manual_visual_review"] = reviewed
        if final.get("mainline_capability_status"):
            final["mainline_capability_status"] = "verified_accepted_usage_scope_visual_review_recorded"
        lines = [line.replace("新增画面尚待统一人工确认", "7张末尾画面已人工核对，其中加120画面仅确认解除过渡，不视为已完全解除的视觉证明") for line in lines]
        lines += ["", "最终7张人工观察已存档：次日60分钟画面确认官方限制解除、家长控制开启、显示剩余1小时0分钟；无额度首次启用前两张为HOME，默认60分钟及两次恢复画面为官方限制提示。加120截图为半透明限制提示，用户推测正在解除；HOME完全显示、视觉完全解除和无额度均未确认。独立机器结果为额度180分钟、restricted=false及随后62.60秒正常增长。画面核对已完成，无需为这张过渡画面重跑设备实验。"]
    final["input_hashes"] = inputs
    dump(out / "final-evidence.json", final)
    lines += ["", "## 画面人工观察", "", "此前8组画面已核对；新增画面的人工观察单独存档，不修改截图及原始计数。画面确认不替代真正首次接管的现场或次日可用增长窗口。", ""]
    pictures = [("初始受限", "screenshots/chain-base-restricted.jpg"), ("第二次加时解除后", "screenshots/chain-second-grant-clear.jpg"), ("构造重新接管前", "retakeover/screenshots/retakeover-before-enable.jpg"), ("生命周期完成后", "lifecycle/screenshots/lifecycle-after-batch.jpg")]
    body = '<!doctype html><meta charset="utf-8"><title>PCTL1952主线实测</title><style>body{font:16px system-ui;max-width:1100px;margin:24px;background:#f4f7fb;color:#182740}pre{white-space:pre-wrap;line-height:1.65}img{width:100%;max-width:800px}a{color:#2464cb}</style><p><a href="final-evidence.json">原始端点与输入哈希</a> · <a href="plan.md">通过标准与执行顺序</a></p><pre>' + html.escape("\n".join(lines)) + '</pre>'
    for label, relative in pictures:
        path = live / relative
        if path.exists():
            link = "live-capture/"+relative
            observation = visual_by_image.get(link)
            confirmed = observation is not None and observation.get("status") == "human_confirmed"
            state = "已人工确认" if confirmed else "待最终人工核对"
            detail = "：" + observation["observation"] if confirmed else ""
            lines.append(f'- [{label}，{state}]({link}){detail}')
            body += f'<h2>{html.escape(label)}：{state}</h2>'
            if confirmed:
                body += '<p>'+html.escape(observation["observation"])+'</p>'
            body += f'<img src="{html.escape(link)}">'
    (out / "final-findings.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    (out / "index.html").write_text(body, encoding="utf-8")
    return final


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    result = report(parser.parse_args().root)
    print(json.dumps({"first_takeover": result["true_first_takeover"]["status"], "lifecycle": result.get("lifecycle_verdict", {}).get("status", "pending")}, ensure_ascii=False))
