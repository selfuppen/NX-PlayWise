#!/usr/bin/env python3
"""Autonomous real-device capability probe (lab-only).

Drives the Device Lab session end to end over FTP (tools/lab_wire_client) and
captures real-device screenshots over sys-botbase (tools/sys_botbase_client),
with timing synchronized on the device's own lab/session.json state -- never on
reading the screen. It never submits a lab_observation and never seals a verdict:
it is a probe, not the authoritative qualification gate
(verify_device_qualification).

Scenarios split into two shapes:
  * restriction_quick / timer_activation_ab -- capture-only: drive lab phases +
    screenshots, pull the device's own machine JSON, judge nothing.
  * bedtime / eye_care -- write-and-evaluate: confirm, write the product policy
    (set_bedtime_policy / set_eye_care_policy), read it back in `status` to prove
    the sysmodule accepts and *evaluates* it, then disable again (try/finally, so
    a mid-run failure never leaves the console locked). The lab build compiles out
    the autonomous enforce tick (sysmodule_core.c:632 #ifndef PLAYWISE_DEVICE_LAB),
    so the visible block/break never fires here; it enforces via the same PCTL
    BLOCKED path as restriction_quick (already proven), only under the standard
    build -- so visible interception is verified by the human-attested gate, not
    this probe. These two mutate device state and are excluded from
    --scenario all; select them explicitly.

Lab-only: this drives the safety gates the product requires; run it only against
a dedicated test console, never a managed child's device.
"""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import datetime
import hashlib
import json
import tempfile
from dataclasses import dataclass, field
import time


import tools.automation.sys_botbase_client as sbb  # noqa: E402
import tools.install.install_package_via_ftp as ftp_installer  # noqa: E402
import tools.lab.lab_wire_client as lwc  # noqa: E402
import tools.automation.vision_capture_driver as vcd  # noqa: E402
import tools.automation.probe_report as probe_report  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
LAB_APP = lwc.LAB_APP
HOME_TITLE_ID = "0100000000001000"
ENVIRONMENT_REMOTE = f"switch/{LAB_APP}/environment.json"


class ProbeError(RuntimeError):
    pass


class Progress:
    """Live, timestamped step logger with an elapsed counter and an ETA for the
    next wait, so a long run (timer_activation_ab) shows what it is doing and how
    long each step should take. Console stamps are LOCAL time (matches your wall
    clock); the stored evidence JSON stays UTC."""

    def __init__(self, *, clock=time.monotonic) -> None:
        self._clock = clock
        self.start = clock()

    def log(self, message: str, *, eta_s: float | None = None) -> None:
        elapsed = int(self._clock() - self.start)
        stamp = datetime.datetime.now().strftime("%H:%M:%S")
        eta = f"  预计 ~{int(eta_s)}s" if eta_s is not None else ""
        print(f"[{stamp} +{elapsed}s]{eta}  {message}", flush=True)


def _p(ctx, message: str, *, eta_s: float | None = None) -> None:
    progress = getattr(ctx, "progress", None)
    if progress is not None:
        progress.log(message, eta_s=eta_s)


def estimate_seconds(name: str, *, play_minutes: int = 1, sleep_checkpoint: bool = False) -> int:
    """Rough wall-clock estimate from the compiled window constants, for the
    upfront plan print. timer_activation_ab's two 90s legs dominate."""
    if name == "restriction_quick":
        return 45
    if name == "timer_activation_ab":
        return 90 + 90 + 15 * 5 + 40  # 7 windows + per-phase/setup overhead
    if name == "eye_care":
        return 15
    if name == "bedtime":
        return 20
    return 30


@dataclass
class PreflightResult:
    ftp_port: int
    sysbotbase_title: str
    lab_alive: bool
    pixelpeek_ok: bool
    foreground_is_home: bool
    notes: list = field(default_factory=list)

    def as_dict(self) -> dict:
        return {
            "ftp_port": self.ftp_port,
            "sysbotbase_title": self.sysbotbase_title,
            "lab_alive": self.lab_alive,
            "pixelpeek_ok": self.pixelpeek_ok,
            "foreground_is_home": self.foreground_is_home,
            "notes": list(self.notes),
        }


@dataclass
class ProbeContext:
    wire: object
    sbb: object
    ftp: object
    session_source: object
    reports_dir: Path
    scratch: Path
    result_timeout: float = 20.0
    sampling_timeout: float = 40.0
    phase_slack: float = 45.0
    interactive: bool = False
    sleep: object = time.sleep
    clock: object = time.monotonic
    progress: object = None


def now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _require_ok(res: dict, label: str) -> dict:
    if res.get("status") != "ok":
        raise ProbeError(
            f"{label} 设备返回失败：status={res.get('status')} "
            f"code={res.get('error_code')} reason={res.get('error_reason')}"
        )
    return res


def probe_ftp_port(host: str, candidates=(21, 5000), *, user: str = "anonymous",
                   password: str = "", timeout: int = 10) -> ftp_installer.FtpEndpoint:
    """Connect+login on each candidate port; return the first that works."""
    last = None
    for port in candidates:
        endpoint = ftp_installer.FtpEndpoint(host=host, port=port, user=user,
                                             password=password, base_path="/")
        client = ftp_installer.SwitchFtpClient(endpoint, timeout=timeout)
        try:
            client.connect()
            client.close()
            return endpoint
        except Exception as exc:  # noqa: BLE001 - any connect/login failure means try next
            last = exc
    raise ProbeError(f"FTP 不可达（尝试端口 {list(candidates)}）：{last}")


def _pull_machine(ctx: ProbeContext, scenario_dir: Path, remotes: list) -> dict:
    """Download the named lab evidence files (relative to the lab app root) into
    <scenario_dir>/machine and return {remote: parsed-json | {"_error": ...}}."""
    machine_dir = scenario_dir / "machine"
    machine_dir.mkdir(parents=True, exist_ok=True)
    pulled: dict = {}
    for rel in remotes:
        dest = machine_dir / Path(rel).name
        try:
            ctx.ftp.download_file(f"switch/{LAB_APP}/{rel}", dest)
            pulled[rel] = json.loads(dest.read_text(encoding="utf-8"))
        except Exception as exc:  # noqa: BLE001 - optional evidence; record and continue
            pulled[rel] = {"_error": str(exc)}
    return pulled


def _device_fingerprint(ctx: ProbeContext) -> str:
    """sha256 of the device's environment.json, matching
    sysmodule_environment_fingerprint (sysmodule_storage.c:787). Required so a
    wire-driven confirm_bedtime_requirements is accepted."""
    dest = ctx.scratch / "environment.json"
    ctx.ftp.download_file(ENVIRONMENT_REMOTE, dest)
    return hashlib.sha256(dest.read_bytes()).hexdigest()


def _ensure_bedtime_confirmed(ctx: ProbeContext, wlog) -> None:
    """Both bedtime AND eye-care enablement require the shared bedtime
    confirmation (sysmodule_control.c:215/277; errors 319/323). Submit
    confirm_bedtime_requirements with the device fingerprint so the sysmodule
    records confirmation_version + confirmed_environment + overlay-risk."""
    fingerprint = _device_fingerprint(ctx)
    wlog.record("read environment fingerprint")
    rid = ctx.wire.submit("confirm_bedtime_requirements", lwc.confirm_bedtime_payload(fingerprint))
    _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout),
                "confirm_bedtime_requirements")
    wlog.record("confirm_bedtime_requirements ok")


def _pull_status(ctx: ProbeContext, scenario_dir: Path) -> dict:
    """Submit a product `status` request and save the sysmodule's current view
    (restriction_reasons / bedtime / eye_care). This is the real evidence in lab
    mode: the autonomous enforce tick is compiled out of the Device Lab build
    (sysmodule_core.c:632 `#ifndef PLAYWISE_DEVICE_LAB`), so a written
    bedtime/eye-care policy is accepted and *evaluated* in status but never
    applies a block here -- enforcement runs only under the standard build."""
    rid = ctx.wire.submit("status")
    res = ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout)
    status = dict(res.get("raw") if isinstance(res.get("raw"), dict) else {})
    status.pop("recent_events", None)
    machine_dir = scenario_dir / "machine"
    machine_dir.mkdir(parents=True, exist_ok=True)
    (machine_dir / "status.json").write_text(
        json.dumps(status, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return status


def _capture_burst(ctx: ProbeContext, scenario_dir: Path, *, count: int, interval_s: float,
                   start_index: int, wlog) -> list:
    """Blind periodic screenshots for product-feature effects that have no lab
    session state to gate on (eye-care break, bedtime block). Capture-only."""
    shots: list = []
    index = start_index
    for _ in range(count):
        abs_path = scenario_dir / f"{index:02d}.jpg"
        try:
            ctx.sbb.capture_screenshot(abs_path)
            shots.append(vcd._shot_record(abs_path, ctx.reports_dir, f"sample-{index:02d}", now_iso()))
        except Exception as exc:  # noqa: BLE001
            wlog.record(f"capture {index} skipped: {exc}")
        index += 1
        ctx.sleep(interval_s)
    return shots


def preflight(ctx: ProbeContext, *, require_pixelpeek: bool = True) -> PreflightResult:
    """All automatic, hard-fail-and-stop. Side-effecting steps come last."""
    notes: list = []
    # 1. sys-botbase reachable: a reply to getSystemLanguage proves the socket
    # round-trip works. getTitleID is intentionally NOT the liveness probe --
    # at the HOME menu it legitimately returns empty (that is a foreground note
    # at step 4, not a dead sys-botbase).
    _p(ctx, "preflight ①/④ 连 sys-botbase（读语言/前台标题）")
    try:
        language = (ctx.sbb.get_system_language() or "").strip()
    except sbb.SysBotBaseError as exc:
        raise ProbeError(f"sys-botbase 无响应：{exc}")
    if not language:
        raise ProbeError("sys-botbase 无响应（getSystemLanguage 返回空）")
    title = (ctx.sbb.get_title_id() or "").strip()
    # 2. Lab sysmodule alive AND claiming the inbox: a no-side-effect status
    # round-trip. A timeout means the standard sysmodule is booted (it does not
    # claim the lab queue) -- tell the operator to switch into lab mode.
    _p(ctx, "preflight ②/④ 探 Lab sysmodule 是否认领 inbox", eta_s=ctx.result_timeout)
    rid = ctx.wire.submit("lab_session_status")
    try:
        res = ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout)
    except lwc.WireTimeout as exc:
        raise ProbeError(
            "lab_session_status 无结果：当前多半不是 Lab sysmodule（请在设备上切到 lab 模式），"
            f"或 inbox 未被认领（{exc}）"
        )
    if res.get("status") != "ok":
        raise ProbeError(
            "Lab 在运行但请求被拒："
            f"reason={res.get('error_reason')} code={res.get('error_code')}；"
            "若为 setup 未完成，请先在设备上完成 lab setup"
        )
    # 3. pixelPeek usable (Atmosphere debug flag). Fail loud before any mutation.
    _p(ctx, "preflight ③/④ pixelPeek 预检（Atmosphère debug flag）")
    pixelpeek_ok = True
    if require_pixelpeek:
        probe_shot = ctx.scratch / "_preflight.jpg"
        try:
            ctx.sbb.capture_screenshot(probe_shot)
        except Exception as exc:  # noqa: BLE001
            raise ProbeError(
                f"pixelPeek 不可用（{exc}）：请在 Atmosphère 打开 debug/debugunit flag 后重试"
            )
    # 4. A foreground game is present (restriction/eye-care need one). An empty
    # title or the HOME title id both mean "no game in front".
    foreground_is_home = (not title) or title == HOME_TITLE_ID
    if foreground_is_home:
        notes.append("前台无游戏（HOME 菜单或空标题）：限制生效/护眼场景需要前台有游戏在运行")
    _p(ctx, f"preflight 通过（前台 {title or 'HOME/空'}）")
    return PreflightResult(ctx.ftp.endpoint.port if hasattr(ctx.ftp, "endpoint") else 0,
                           title, True, pixelpeek_ok, foreground_is_home, notes)


def _write_scenario(scenario_dir: Path, record: dict) -> None:
    (scenario_dir / "scenario.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def run_restriction(ctx: ProbeContext) -> dict:
    """restriction_quick: force the system restriction (BLOCKED + start timer)
    and blind-capture the 15s window, gated on session state. No observation is
    submitted -- the run is left in awaiting_observation and judged later."""
    scenario_dir = ctx.reports_dir / "restriction_quick"
    scenario_dir.mkdir(parents=True, exist_ok=True)
    _p(ctx, "restriction_quick 开始", eta_s=estimate_seconds("restriction_quick"))
    wlog = vcd.WireLog()
    ctx.wire.reset_session()
    wlog.record("reset_session")
    baseline = scenario_dir / "00-baseline.jpg"
    ctx.sbb.capture_screenshot(baseline)
    wlog.record("pixelPeek baseline")
    shots = [vcd._shot_record(baseline, ctx.reports_dir, "baseline", now_iso())]
    rid = ctx.wire.submit("lab_session_start", {"mode": "restriction_quick"})
    _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout), "lab_session_start")
    wlog.record("lab_session_start ok")
    rid = ctx.wire.submit("lab_phase_start", {"phase": "restriction_effect"})
    _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout), "lab_phase_start")
    wlog.record("lab_phase_start restriction_effect ok")
    _p(ctx, "限制已下发，盲抓 15s 采样窗口", eta_s=15)
    vcd.wait_for_state(ctx.session_source, lambda s: s.get("state") == "sampling",
                       timeout_s=ctx.sampling_timeout, sleep=ctx.sleep, clock=ctx.clock)
    shots += vcd.capture_sampling_frames(ctx.sbb, ctx.session_source, scenario_dir, ctx.reports_dir,
                                         start_index=1, sleep=ctx.sleep, clock=ctx.clock, now=now_iso)
    wlog.record(f"captured {len(shots)} frames (no observation submitted)")
    _p(ctx, f"restriction_quick 完成，抓 {len(shots)} 帧")
    (scenario_dir / "wire.log").write_text(wlog.text(), encoding="utf-8", newline="\n")
    machine = _pull_machine(ctx, scenario_dir, ["lab/phase-5.json", "lab/public.json"])
    record = {
        "name": "restriction_quick", "title": "限制生效", "status": "captured",
        "note": "关浮窗后 15s 窗口盲抓；运行期未提交观察、未判读。",
        "screenshots": shots, "machine": machine,
        "pending_questions": ["限时提示是否出现？", "游戏是否暂停/挂起/继续？"],
    }
    _write_scenario(scenario_dir, record)
    return record


def _sleep_checkpoint(ctx: ProbeContext) -> bool:
    msg = "请在设备上按电源键进入睡眠 ≥95 秒后唤醒，然后按回车继续（跳过则记为 inconclusive）..."
    if ctx.interactive and sys.stdin and sys.stdin.isatty():
        try:
            input(msg)
            return True
        except EOFError:
            pass
    print("WARN: 非交互运行，跳过真机睡眠腿；设备将如实记 sleep_excluded=inconclusive。", file=sys.stderr)
    return False


def _reconnect(ctx: ProbeContext) -> None:
    """After a real sleep the FTP control socket (and sys-botbase) are torn
    down. Rebuild them in place so the wire/session polling keeps working. The
    SwitchFtpClient object identity is preserved, so FtpSessionStateSource and
    LabWireClient (which hold it) transparently use the new connection."""
    reconnect_ftp = getattr(ctx.ftp, "reconnect", None)
    if reconnect_ftp is None:
        return
    try:
        reconnect_ftp()
    except Exception as exc:  # noqa: BLE001
        raise ProbeError(f"睡眠后重连 FTP 失败：{exc}")
    reconnect_sbb = getattr(ctx.sbb, "reconnect", None)
    if reconnect_sbb:
        try:
            reconnect_sbb()
        except Exception:  # noqa: BLE001 - sys-botbase unused after the sleep leg
            pass


def run_timer_ab(ctx: ProbeContext, *, sleep_checkpoint: bool = False) -> dict:
    """timer_activation_ab: drive the 7 activation phases in order and pull the
    machine numbers (1454/1952/0x44/fallback). Fully hands-off except the
    ab_sleep_wake leg, which needs a real sleep (discrete checkpoint or skipped
    as inconclusive)."""
    scenario_dir = ctx.reports_dir / "timer_activation_ab"
    scenario_dir.mkdir(parents=True, exist_ok=True)
    _p(ctx, "timer_activation_ab 开始（7 相位，两条 90s 腿为主要耗时）",
       eta_s=estimate_seconds("timer_activation_ab"))
    wlog = vcd.WireLog()
    ctx.wire.reset_session()
    wlog.record("reset_session")
    shots = []
    baseline = scenario_dir / "00-baseline.jpg"
    try:
        ctx.sbb.capture_screenshot(baseline)
        shots.append(vcd._shot_record(baseline, ctx.reports_dir, "baseline", now_iso()))
        wlog.record("pixelPeek baseline")
    except Exception as exc:  # noqa: BLE001 - machine-only scenario; baseline is a nicety
        wlog.record(f"baseline capture skipped: {exc}")
    rid = ctx.wire.submit("lab_session_start", {"mode": "timer_activation_ab"})
    _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout), "lab_session_start")
    wlog.record("lab_session_start ok")
    phases_run: list = []
    guard = 0
    while True:
        guard += 1
        if guard > 32:
            raise ProbeError("timer_activation_ab 相位推进超过上限（疑似卡死）")
        session = ctx.session_source.read() or {}
        state = session.get("state")
        if state == "complete":
            break
        if state in ("error", "restore_required"):
            raise ProbeError(f"timer_activation_ab 设备进入 {state}")
        if state != "ready":
            vcd.wait_for_state(ctx.session_source,
                               lambda s: s.get("state") in ("ready", "complete", "error", "restore_required"),
                               timeout_s=ctx.phase_slack, sleep=ctx.sleep, clock=ctx.clock)
            continue
        phase = lwc.expected_phase(session)
        if phase is None:
            break
        current_n = session.get("next_phase")
        rid = ctx.wire.submit("lab_phase_start", {"phase": phase})
        _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout),
                    f"lab_phase_start {phase}")
        phases_run.append(phase)
        wlog.record(f"phase {phase} -> sampling")
        window = lwc.phase_window_seconds(phase)
        _p(ctx, f"相位 {len(phases_run)}/7 {phase}，采样 ~{window}s", eta_s=window)
        # The ab_sleep_wake window must elapse with the console ASLEEP. The phase
        # is submitted FIRST (its before-sample is taken while awake); only then
        # does the operator sleep >=95s and wake. FTP/sys-botbase sockets drop
        # during sleep, so reconnect before polling the result.
        if phase == "ab_sleep_wake" and sleep_checkpoint:
            _p(ctx, "ab_sleep_wake：看到下方提示请【立刻】让主机睡眠 ≥95s 再唤醒回车")
            _sleep_checkpoint(ctx)
            _reconnect(ctx)
            wlog.record("reconnected after sleep leg")
        vcd.wait_for_state(
            ctx.session_source,
            lambda s, n=current_n: s.get("state") == "complete"
            or (s.get("state") in ("error", "restore_required"))
            or (s.get("state") == "ready" and isinstance(s.get("next_phase"), int)
                and (not isinstance(n, int) or s.get("next_phase") > n)),
            timeout_s=window + ctx.phase_slack, sleep=ctx.sleep, clock=ctx.clock)
    (scenario_dir / "wire.log").write_text(wlog.text(), encoding="utf-8", newline="\n")
    machine = _pull_machine(ctx, scenario_dir,
                            [f"lab/phase-{i}.json" for i in range(7)] + ["lab/public.json"])
    _p(ctx, f"timer_activation_ab 完成，{len(phases_run)} 相位，已拉机器数字")
    record = {
        "name": "timer_activation_ab", "title": "待机计时 A/B", "status": "complete",
        "note": "7 相位机器证据（1454/1952/0x44/fallback）；睡眠腿未真机休眠时设备记 inconclusive。",
        "screenshots": shots, "machine": machine, "phases_run": phases_run,
        "pending_questions": ["sleep_excluded 是否为 1（待机停止计时）？",
                              "三个 fallback 分支是否符合预期？"],
    }
    _write_scenario(scenario_dir, record)
    return record


def run_eye_care(ctx: ProbeContext, *, play_minutes: int = 1, rest_minutes: int = 1) -> dict:
    """Write-and-evaluate probe: confirm, enable a short eye-care policy, read
    the sysmodule `status` (proves the policy is accepted and evaluated), then
    disable it again. The lab build compiles out the autonomous enforce tick
    (sysmodule_core.c:632 #ifndef PLAYWISE_DEVICE_LAB), so the rest break never
    fires here; it enforces via the same PCTL BLOCKED path as restriction_quick
    (already proven), only under the standard build."""
    scenario_dir = ctx.reports_dir / "eye_care"
    scenario_dir.mkdir(parents=True, exist_ok=True)
    _p(ctx, "eye_care 开始（写入+求值探测）", eta_s=estimate_seconds("eye_care"))
    wlog = vcd.WireLog()
    shots: list = []
    baseline = scenario_dir / "00-baseline.jpg"
    try:
        ctx.sbb.capture_screenshot(baseline)
        shots.append(vcd._shot_record(baseline, ctx.reports_dir, "baseline", now_iso()))
        wlog.record("pixelPeek baseline")
    except Exception as exc:  # noqa: BLE001
        wlog.record(f"baseline capture skipped: {exc}")
    _p(ctx, "确认前置要求（读指纹）")
    _ensure_bedtime_confirmed(ctx, wlog)
    enabled = False
    status: dict = {}
    try:
        rid = ctx.wire.submit("set_eye_care_policy", lwc.eye_care_payload(play_minutes, rest_minutes))
        _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout), "set_eye_care_policy")
        enabled = True
        wlog.record(f"set_eye_care_policy ok (play={play_minutes}m rest={rest_minutes}m)")
        _p(ctx, "读 status 查看护眼求值")
        status = _pull_status(ctx, scenario_dir)
    finally:
        # Always disable again so a mid-run failure (FTP drop / timeout) never
        # leaves the test console with eye-care stuck on. Best-effort: swallow and
        # log any cleanup failure so it never masks the original error.
        if enabled:
            try:
                rid = ctx.wire.submit("set_eye_care_policy",
                                      lwc.eye_care_payload(play_minutes, rest_minutes, enabled=False))
                _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout),
                            "set_eye_care_policy(disable)")
                wlog.record("set_eye_care_policy disabled (cleanup)")
            except Exception as exc:  # noqa: BLE001 - cleanup must not mask the real error
                wlog.record(f"set_eye_care_policy disable failed (cleanup): {exc}")
        (scenario_dir / "wire.log").write_text(wlog.text(), encoding="utf-8", newline="\n")
    _p(ctx, "eye_care 完成（已写入并求值；lab 构建不自动执行，探测后已关闭）")
    record = {
        "name": "eye_care", "title": "护眼", "status": "written",
        "note": "已确认并写入护眼策略、读 status 验证求值；lab 构建编译掉了自动 enforce_tick，"
                "故休息浮窗不会在 lab 下出现。休息经由与 restriction_quick 相同的 PCTL BLOCKED 执行"
                "（已验证），仅在标准构建下自动触发。探测后已关闭护眼。",
        "screenshots": shots, "machine": {"status": status},
        "pending_questions": ["status.eye_care 是否 enabled 且参数正确（求值生效）？",
                              "（可见休息请在标准构建上验证）"],
    }
    _write_scenario(scenario_dir, record)
    return record


def run_bedtime(ctx: ProbeContext, *, start_minute: int = 1, end_minute: int = 0) -> dict:
    """Write-and-evaluate probe: confirm, apply an immediate always-on bedtime
    window, read `status` (restriction_reasons.bedtime should be true), then
    DISABLE bedtime again so a later standard boot is not locked out by the test
    window. The lab build compiles out enforce_tick (sysmodule_core.c:632), so no
    block appears here; bedtime enforces via the same PCTL BLOCKED path as
    restriction_quick (already proven), only under the standard build."""
    scenario_dir = ctx.reports_dir / "bedtime"
    scenario_dir.mkdir(parents=True, exist_ok=True)
    _p(ctx, "bedtime 开始（写入+求值探测，结束会自动关闭就寝）", eta_s=estimate_seconds("bedtime"))
    wlog = vcd.WireLog()
    shots: list = []
    baseline = scenario_dir / "00-baseline.jpg"
    try:
        ctx.sbb.capture_screenshot(baseline)
        shots.append(vcd._shot_record(baseline, ctx.reports_dir, "baseline", now_iso()))
        wlog.record("pixelPeek baseline")
    except Exception as exc:  # noqa: BLE001
        wlog.record(f"baseline capture skipped: {exc}")
    _ensure_bedtime_confirmed(ctx, wlog)
    enabled = False
    status: dict = {}
    try:
        rid = ctx.wire.submit("set_bedtime_policy",
                              lwc.bedtime_payload(start_minute, end_minute, activation="immediate"))
        _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout), "set_bedtime_policy")
        enabled = True
        wlog.record("set_bedtime_policy ok (immediate, all-day)")
        _p(ctx, "读 status 查看就寝求值")
        status = _pull_status(ctx, scenario_dir)
    finally:
        # Always disable again so a mid-run failure never leaves the standard
        # build locked out by an all-day test window. Best-effort: swallow and log
        # any cleanup failure so it never masks the original error.
        if enabled:
            try:
                rid = ctx.wire.submit("set_bedtime_policy",
                                      lwc.bedtime_payload(start_minute, end_minute, enabled=False))
                _require_ok(ctx.wire.wait_result(rid, ctx.scratch, timeout_s=ctx.result_timeout),
                            "set_bedtime_policy(disable)")
                wlog.record("set_bedtime_policy disabled (cleanup)")
            except Exception as exc:  # noqa: BLE001 - cleanup must not mask the real error
                wlog.record(f"set_bedtime_policy disable failed (cleanup): {exc}")
        (scenario_dir / "wire.log").write_text(wlog.text(), encoding="utf-8", newline="\n")
    _p(ctx, "bedtime 完成（已写入并求值并关闭就寝）")
    record = {
        "name": "bedtime", "title": "就寝", "status": "written",
        "note": "已确认并写入立即生效的全天就寝窗口、读 status 验证求值（restriction_reasons.bedtime）；"
                "lab 构建编译掉了自动 enforce_tick，故就寝拦截不会在 lab 下出现。就寝经由与 "
                "restriction_quick 相同的 PCTL BLOCKED 执行（已验证），仅在标准构建下自动触发。"
                "探测后已关闭就寝，不给标准构建留下全天锁定。",
        "screenshots": shots, "machine": {"status": status},
        "pending_questions": ["status.restriction_reasons.bedtime 是否为 true（求值生效）？",
                              "（可见拦截请在标准构建上验证）"],
    }
    _write_scenario(scenario_dir, record)
    return record


SCENARIOS = {
    "restriction_quick": run_restriction,
    "timer_activation_ab": lambda ctx: run_timer_ab(ctx),
    "bedtime": run_bedtime,
    "eye_care": run_eye_care,
}


def _resolve_endpoint(args) -> ftp_installer.FtpEndpoint:
    if args.url:
        return ftp_installer.FtpEndpoint.parse(args.url)
    return probe_ftp_port(args.host, tuple(args.ftp_ports), timeout=args.ftp_timeout)


def _build_context(args, ftp, sbb_client, scratch: Path, reports_dir: Path) -> ProbeContext:
    source = vcd.FtpSessionStateSource(ftp, scratch)
    wire = lwc.LabWireClient(ftp)
    return ProbeContext(wire=wire, sbb=sbb_client, ftp=ftp, session_source=source,
                        reports_dir=reports_dir, scratch=scratch,
                        interactive=bool(sys.stdin and sys.stdin.isatty()),
                        progress=Progress())


def _default_reports_dir(args) -> Path:
    if args.reports:
        return Path(args.reports)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return ROOT / "device-reports" / f"probe-{stamp}"


def _scenarios_for(selection: str) -> list:
    if selection == "all":
        # bedtime/eye_care are deliberately NOT in `all`: they are write-and-
        # evaluate probes that mutate device policy, so they must be selected
        # explicitly rather than swept in with the capture-only scenarios.
        return ["restriction_quick", "timer_activation_ab"]
    return [selection]


def run_preflight_cmd(args) -> int:
    endpoint = _resolve_endpoint(args)
    scratch = Path(tempfile.mkdtemp(prefix="playwise-probe-"))
    with ftp_installer.SwitchFtpClient(endpoint, timeout=args.ftp_timeout) as ftp:
        with sbb.SysBotBaseClient(args.host, args.port, args.timeout) as sbb_client:
            ctx = _build_context(args, ftp, sbb_client, scratch, _default_reports_dir(args))
            result = preflight(ctx, require_pixelpeek=not args.no_pixelpeek)
    print(f"PASS: preflight ok，FTP 端口 {endpoint.port}，前台 {result.sysbotbase_title}")
    for note in result.notes:
        print(f"  note: {note}")
    return 0


def run_probe_cmd(args) -> int:
    endpoint = _resolve_endpoint(args)
    reports_dir = _default_reports_dir(args)
    reports_dir.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="playwise-probe-"))
    records: list = []
    names = _scenarios_for(args.scenario)
    total = sum(estimate_seconds(n, sleep_checkpoint=args.sleep_checkpoint) for n in names)
    print(f"计划：{', '.join(names)}；预计合计 ~{total}s（约 {max(1, round(total / 60))} 分钟）。"
          "下面每步带本机时间戳 + 预计耗时；证据 JSON 内为 UTC。", flush=True)
    with ftp_installer.SwitchFtpClient(endpoint, timeout=args.ftp_timeout) as ftp:
        with sbb.SysBotBaseClient(args.host, args.port, args.timeout) as sbb_client:
            ctx = _build_context(args, ftp, sbb_client, scratch, reports_dir)
            pre = preflight(ctx, require_pixelpeek=not args.no_pixelpeek)
            for name in names:
                if name == "timer_activation_ab":
                    records.append(run_timer_ab(ctx, sleep_checkpoint=args.sleep_checkpoint))
                else:
                    records.append(SCENARIOS[name](ctx))
            elapsed = int(time.monotonic() - ctx.progress.start)
    (reports_dir / "probe.json").write_text(
        json.dumps({"generated_at": now_iso(), "preflight": pre.as_dict(),
                    "scenarios": [r["name"] for r in records]}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8", newline="\n")
    out = probe_report.render_report_html(reports_dir, records, inline=args.inline,
                                          environment=pre.as_dict())
    print(f"PASS: 已采集 {len(records)} 个场景，实际用时 ~{elapsed}s；复核包 {out}")
    return 0


def run_report_cmd(args) -> int:
    reports_dir = Path(args.reports)
    records = []
    for scenario_json in sorted(reports_dir.glob("*/scenario.json")):
        records.append(json.loads(scenario_json.read_text(encoding="utf-8")))
    if not records:
        raise ProbeError(f"{reports_dir} 下未找到 scenario.json")
    out = probe_report.render_report_html(reports_dir, records, inline=args.inline)
    print(f"PASS: 复核包 {out}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="真机能力探测（lab-only，只采集不判读）。")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_device(p: argparse.ArgumentParser) -> None:
        p.add_argument("--host", required=True, help="sys-botbase host（测试机 IP）")
        p.add_argument("--port", type=int, default=sbb.DEFAULT_PORT, help="sys-botbase 端口（默认 6000）")
        p.add_argument("--timeout", type=float, default=10.0, help="sys-botbase socket 超时")
        p.add_argument("--url", "-u", default=None, help="Switch FTP URL（如 ftp://IP:21/）；省略则探测端口")
        p.add_argument("--ftp-ports", type=int, nargs="+", default=[21, 5000], help="省略 --url 时按序探测的端口")
        p.add_argument("--ftp-timeout", type=int, default=30)
        p.add_argument("--no-pixelpeek", action="store_true", help="跳过 pixelPeek 预检（debug flag 未打时）")

    p_pre = sub.add_parser("preflight", help="自检：FTP/sys-botbase/lab存活/pixelPeek/前台")
    add_device(p_pre)
    p_pre.add_argument("--reports", default=None)
    p_pre.set_defaults(func=run_preflight_cmd)

    p_probe = sub.add_parser("probe", help="跑一个或全部场景并产出复核包")
    add_device(p_probe)
    p_probe.add_argument("--scenario", required=True,
                         choices=["restriction_quick", "timer_activation_ab", "bedtime", "eye_care", "all"],
                         help="bedtime/eye_care 为写入型（改设备策略，结束自动关闭），不含在 all 中，需显式选择")
    p_probe.add_argument("--reports", default=None, help="证据目录（默认 device-reports/probe-<时间戳>）")
    p_probe.add_argument("--sleep-checkpoint", action="store_true",
                         help="在 ab_sleep_wake 停下提示真机睡眠（否则记 inconclusive）")
    p_probe.add_argument("--inline", action="store_true", help="复核包内联 base64 截图成单文件")
    p_probe.set_defaults(func=run_probe_cmd)

    p_report = sub.add_parser("report", help="从已采集的 scenario.json 重新生成复核包")
    p_report.add_argument("--reports", required=True)
    p_report.add_argument("--inline", action="store_true")
    p_report.set_defaults(func=run_report_cmd)
    return parser


def main(argv: list | None = None) -> int:
    vcd.configure_console()
    banner = getattr(getattr(vcd, "smoke", None), "LAB_ONLY_BANNER", None)
    if banner:
        print(banner)
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ProbeError, lwc.WireError, sbb.SysBotBaseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())


