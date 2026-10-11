#!/usr/bin/env python3
"""sys-botbase vision capture driver for the Device Lab campaign (lab-only).

Drives a Device Lab campaign slot's NORMAL run over sys-botbase by BUTTONS
(position-independent, unlike touch which the overlay shifts with a magic
offset), captures real-device screenshots with pixelPeek across the 15s
restriction window, and leaves a pending.json for the AI agent (Claude Code)
to read and seal with tools/vision_verdict.py. After sealing, the `submit`
subcommand drives the on-device observation UI per the sealed verdict so the
device app writes the authoritative schema-v2 report.

Lab-only, like the input smoke harness: sys-botbase can drive the very
physical-hold safety gates the product requires, so this must only ever run
against a dedicated test console -- never a managed child's device. The run is
parked in `awaiting_observation` with the game already restored before capture
ends, so the agent can take as long as it needs; the restriction is live only
during the 15s sampling window, so the captured frames are the sole evidence.

Timing is synchronized on the device's own lab/session.json (polled over FTP to
a scratch path, never into the manifest-tracked evidence tree), not fixed
sleeps: state `sampling` -> capture; `awaiting_observation` -> stop and hand off.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sys_botbase_client as sbb  # noqa: E402
import input_smoke_harness as smoke  # noqa: E402
import install_package_via_ftp as ftp_installer  # noqa: E402
from vision_verdict import OBSERVATIONS, RUNTIME_EFFECTS  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
LAB_APP = ftp_installer.LAB_APP_NAME
SESSION_REMOTE = f"switch/{LAB_APP}/lab/session.json"

# Device Lab danger-confirm requires a held A >= 2.0s (device_lab/overlay/source/
# main.cpp:263 `elapsed >= 2000000000`); the input smoke harness's 1200ms is too
# short. The restriction sampling window is LAB_RESTRICTION_SECONDS = 15.
DANGER_HOLD_MS = 2300
SAMPLING_SECONDS = 15
LAUNCH_COMBO = smoke.DEFAULT_LAUNCH_COMBO

# On-device observation UI option order (main.cpp submit_observation): the
# cursor starts at index 0 on each step, so N DDOWN presses select index N.
OBSERVATION_INDEX = {"restriction_visible": 0, "no_visible_restriction": 1, "unsure": 2}
RUNTIME_INDEX = {"continued": 0, "paused_or_suspended": 1, "exited": 2, "unsure": 3}


@dataclass
class SlotPlan:
    index: int
    slot: str
    game_slot: str
    official_pause_expected: str
    needs_vision: bool


# Campaign slots in gate order (verify_device_qualification.CAMPAIGN_SLOTS).
# Slot 0 (timer_activation_ab) is pure machine evidence -- it never enters
# awaiting_observation, so it needs no screenshot/verdict.
SLOT_PLANS = (
    SlotPlan(0, "timer_activation_ab", "none", "not_applicable", False),
    SlotPlan(1, "pause_on_game_a", "a", "on", True),
    SlotPlan(2, "pause_on_game_b", "b", "on", True),
    SlotPlan(3, "pause_off_game_b", "b", "off", True),
)
VISION_SLOTS = {plan.slot: plan for plan in SLOT_PLANS if plan.needs_vision}


class DriveError(RuntimeError):
    pass


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            try:
                reconfigure(errors="replace")
            except (ValueError, OSError):
                pass


def now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class WireLog:
    """Timestamped record of every wire action, hashed into pending.json."""

    def __init__(self, now=now_iso) -> None:
        self.now = now
        self.lines: list[str] = []

    def record(self, label: str) -> None:
        self.lines.append(f"{self.now()}\t{label}")

    def text(self) -> str:
        return "\n".join(self.lines) + ("\n" if self.lines else "")


def run_steps(client, steps, wire: WireLog, sleep=time.sleep) -> None:
    for step in steps:
        label = smoke.execute_step(client, step, sleep=sleep)
        wire.record(label)


class SessionStateSource:
    """Reads the device's lab/session.json. Subclasses supply the transport."""

    def read(self) -> dict:
        raise NotImplementedError


class FtpSessionStateSource(SessionStateSource):
    """Downloads lab/session.json over the existing FTP client into a scratch
    path (never the manifest-tracked evidence tree) and parses it."""

    def __init__(self, client, scratch_dir: Path, remote: str = SESSION_REMOTE) -> None:
        self.client = client
        self.scratch_dir = Path(scratch_dir)
        self.remote = remote

    def read(self) -> dict:
        self.scratch_dir.mkdir(parents=True, exist_ok=True)
        dest = self.scratch_dir / "session.json"
        try:
            self.client.download_file(self.remote, dest)
            value = json.loads(dest.read_text(encoding="utf-8"))
        except Exception:
            return {}
        return value if isinstance(value, dict) else {}


def wait_for_state(source: SessionStateSource, predicate, *, timeout_s: float,
                   poll_s: float = 1.0, sleep=time.sleep, clock=time.monotonic) -> dict:
    deadline = clock() + timeout_s
    last: dict = {}
    while True:
        last = source.read() or {}
        if predicate(last):
            return last
        if clock() >= deadline:
            raise DriveError(f"等待设备状态超时；最后 state={last.get('state')!r}")
        sleep(poll_s)


def _shot_record(abs_path: Path, reports_dir: Path, checkpoint: str, captured_at: str) -> dict:
    return {
        "checkpoint": checkpoint,
        "path": abs_path.relative_to(reports_dir).as_posix(),
        "sha256": sha256_file(abs_path),
        "captured_at": captured_at,
    }


def capture_sampling_frames(client, source: SessionStateSource, run_dir: Path,
                            reports_dir: Path, *, max_frames: int = 12, interval_s: float = 1.2,
                            start_index: int = 1, sleep=time.sleep, clock=time.monotonic,
                            now=now_iso) -> list[dict]:
    """Capture dense frames while the device reports state `sampling`; stop once
    it leaves that state (restriction reverted -> awaiting_observation) after at
    least one frame, or when max_frames / the window+slack deadline is reached."""
    shots: list[dict] = []
    index = start_index
    deadline = clock() + SAMPLING_SECONDS + 5
    while len(shots) < max_frames and clock() < deadline:
        state = (source.read() or {}).get("state")
        if state == "sampling":
            abs_path = run_dir / f"{index:02d}.jpg"
            client.capture_screenshot(abs_path)
            shots.append(_shot_record(abs_path, reports_dir, f"sampling-{index:02d}", now()))
            index += 1
            sleep(interval_s)
        elif shots:
            break
        else:
            sleep(interval_s)
    return shots


def write_pending(reports_dir: Path, run_dir: Path, *, run_id: str, campaign_id, slot,
                  sysbotbase_commit: str, wire: WireLog, screenshots: list[dict],
                  now=now_iso) -> Path:
    wire_log_path = run_dir / "wire.log"
    wire_log_path.write_text(wire.text(), encoding="utf-8", newline="\n")
    pending = {
        "schema_version": 1,
        "run_id": run_id,
        "campaign_id": campaign_id,
        "slot": slot,
        "sysbotbase_commit": sysbotbase_commit,
        "wire_log": wire_log_path.relative_to(reports_dir).as_posix(),
        "wire_log_sha256": sha256_file(wire_log_path),
        "captured_at": now(),
        "question": {
            "observation": "系统限制提示是否出现？取值 " + "|".join(OBSERVATIONS),
            "runtime_effect": "游戏实际行为？取值 " + "|".join(RUNTIME_EFFECTS),
        },
        "screenshots": screenshots,
    }
    out = run_dir / "pending.json"
    out.write_text(json.dumps(pending, ensure_ascii=False, indent=2) + "\n",
                   encoding="utf-8", newline="\n")
    return out


def observation_steps(observation: str, runtime_effect: str) -> list[smoke.SmokeStep]:
    """Button choreography for the on-device observation UI: DDOWN to the option
    index, A to advance step0->step1, DDOWN again, A to submit (main.cpp:233-248)."""
    if observation not in OBSERVATION_INDEX:
        raise DriveError(f"observation 非法：{observation!r}")
    if runtime_effect not in RUNTIME_INDEX:
        raise DriveError(f"runtime_effect 非法：{runtime_effect!r}")
    steps: list[smoke.SmokeStep] = []
    for _ in range(OBSERVATION_INDEX[observation]):
        steps.append(smoke.SmokeStep("click", {"button": "DDOWN"}, "move to observation option"))
    steps.append(smoke.SmokeStep("click", {"button": "A"}, "confirm observation -> step1"))
    for _ in range(RUNTIME_INDEX[runtime_effect]):
        steps.append(smoke.SmokeStep("click", {"button": "DDOWN"}, "move to runtime-effect option"))
    steps.append(smoke.SmokeStep("click", {"button": "A"}, "submit observation"))
    return steps


def drive_slot(client, source: SessionStateSource, plan: SlotPlan, reports_dir: Path, *,
               nav_steps, sysbotbase_commit: str, launch_combo=LAUNCH_COMBO,
               danger_hold_ms: int = DANGER_HOLD_MS, max_frames: int = 12,
               interval_s: float = 1.2, run_id_timeout_s: float = 60.0,
               sampling_timeout_s: float = 30.0, sleep=time.sleep,
               clock=time.monotonic, now=now_iso, wire: WireLog | None = None) -> dict:
    """Drive one vision slot end to end: preflight+baseline, open overlay, nav to
    the dangerous-confirm screen, hold A >= 2.0s, close overlay, then capture the
    15s sampling window gated on session.json. Leaves a pending.json for the AI."""
    if not plan.needs_vision:
        raise DriveError(f"slot {plan.slot} 无需视觉抓图（纯机器证据）")
    wire = wire or WireLog(now=now)
    vision_root = reports_dir / "vision"
    vision_root.mkdir(parents=True, exist_ok=True)
    # Preflight + baseline: one capture while the game is foregrounded, before any
    # overlay/PCTL action. Doubles as the pixelPeek SOI preflight (raises loudly
    # on the debug-flag failure before anything is mutated).
    baseline_tmp = vision_root / "_baseline.jpg"
    client.capture_screenshot(baseline_tmp)
    wire.record("pixelPeek baseline")
    run_steps(client, [smoke.SmokeStep("combo_hold",
              {"buttons": list(launch_combo), "hold_ms": 400}, "open overlay")], wire, sleep=sleep)
    run_steps(client, nav_steps, wire, sleep=sleep)
    state = wait_for_state(source, lambda s: bool(s.get("run_id")),
                           timeout_s=run_id_timeout_s, sleep=sleep, clock=clock)
    run_id = str(state["run_id"])
    run_dir = vision_root / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    baseline_path = run_dir / "00-baseline.jpg"
    baseline_tmp.replace(baseline_path)
    screenshots = [_shot_record(baseline_path, reports_dir, "baseline", now())]
    # Danger confirm: physical hold of A >= 2.0s; touch is rejected on this screen.
    run_steps(client, [smoke.SmokeStep("hold",
              {"button": "A", "hold_ms": danger_hold_ms}, "danger confirm hold A")], wire, sleep=sleep)
    run_steps(client, [smoke.SmokeStep("click", {"button": "B"}, "close overlay")], wire, sleep=sleep)
    wait_for_state(source, lambda s: s.get("state") == "sampling",
                   timeout_s=sampling_timeout_s, sleep=sleep, clock=clock)
    screenshots += capture_sampling_frames(client, source, run_dir, reports_dir,
                                           max_frames=max_frames, interval_s=interval_s,
                                           start_index=1, sleep=sleep, clock=clock, now=now)
    pending = write_pending(reports_dir, run_dir, run_id=run_id,
                            campaign_id=state.get("campaign_id"), slot=plan.slot,
                            sysbotbase_commit=sysbotbase_commit, wire=wire,
                            screenshots=screenshots, now=now)
    return {"run_id": run_id, "pending": str(pending), "frames": len(screenshots),
            "run_dir": str(run_dir)}


def submit_observation(client, source: SessionStateSource, reports_dir: Path, run_id: str, *,
                       launch_combo=LAUNCH_COMBO, timeout_s: float = 120.0, sleep=time.sleep,
                       clock=time.monotonic, now=now_iso, wire: WireLog | None = None) -> dict:
    """After sealing, reopen the overlay and submit the sealed observation via the
    on-device UI so the device app writes the authoritative report."""
    verdict_path = reports_dir / f"{run_id}.vision.json"
    if not verdict_path.is_file():
        raise DriveError(f"缺少已封存判定：{verdict_path}（先运行 vision_verdict seal）")
    verdict = json.loads(verdict_path.read_text(encoding="utf-8"))
    wire = wire or WireLog(now=now)
    wait_for_state(source, lambda s: s.get("state") == "awaiting_observation",
                   timeout_s=timeout_s, sleep=sleep, clock=clock)
    run_steps(client, [smoke.SmokeStep("combo_hold",
              {"buttons": list(launch_combo), "hold_ms": 400}, "reopen overlay")], wire, sleep=sleep)
    run_steps(client, observation_steps(verdict["observation"], verdict["runtime_effect"]),
              wire, sleep=sleep)
    return {"run_id": run_id, "observation": verdict["observation"],
            "runtime_effect": verdict["runtime_effect"], "wire": list(wire.lines)}


def _load_nav(args) -> list[smoke.SmokeStep]:
    if args.scenario:
        return smoke.load_scenario(args.scenario)
    print("WARN: 未提供 --scenario：默认只开 Overlay，不含到达危险确认屏的导航；"
          "真机上几乎肯定需要用 --scenario 提供本固件/overlay 版本的按键路径。", file=sys.stderr)
    return []


def run_drive(args) -> int:
    plan = VISION_SLOTS[args.slot]
    nav_steps = _load_nav(args)
    scratch = Path(tempfile.mkdtemp(prefix="playwise-session-"))
    endpoint = ftp_installer.FtpEndpoint.parse(args.url)
    with ftp_installer.SwitchFtpClient(endpoint, timeout=args.ftp_timeout) as ftp_client:
        source = FtpSessionStateSource(ftp_client, scratch)
        with sbb.SysBotBaseClient(args.host, args.port, args.timeout) as client:
            result = drive_slot(client, source, plan, args.reports.resolve(),
                                nav_steps=nav_steps, sysbotbase_commit=args.sysbotbase_commit,
                                max_frames=args.max_frames, interval_s=args.interval)
    print(f"已抓 {result['frames']} 帧；pending={result['pending']}")
    print("下一步：让 Claude Code 读该 run 的截图判读，再 "
          f"`python tools/vision_verdict.py seal --reports {args.reports} --run-id {result['run_id']} ...`，"
          f"随后 `python tools/vision_capture_driver.py submit --run-id {result['run_id']} ...`。")
    return 0


def run_submit(args) -> int:
    scratch = Path(tempfile.mkdtemp(prefix="playwise-session-"))
    endpoint = ftp_installer.FtpEndpoint.parse(args.url)
    with ftp_installer.SwitchFtpClient(endpoint, timeout=args.ftp_timeout) as ftp_client:
        source = FtpSessionStateSource(ftp_client, scratch)
        with sbb.SysBotBaseClient(args.host, args.port, args.timeout) as client:
            result = submit_observation(client, source, args.reports.resolve(), args.run_id)
    print(f"已按判定提交观察：{result['observation']} / {result['runtime_effect']}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="sys-botbase 视觉抓图驱动（lab-only）。")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p: argparse.ArgumentParser) -> None:
        p.add_argument("--host", required=True, help="sys-botbase host（测试机 IP）")
        p.add_argument("--port", type=int, default=sbb.DEFAULT_PORT)
        p.add_argument("--timeout", type=float, default=10.0)
        p.add_argument("--url", "-u", default=ftp_installer.DEFAULT_FTP_URL,
                       help="Switch FTP URL（读 lab/session.json）")
        p.add_argument("--ftp-timeout", type=int, default=30)
        p.add_argument("--reports", type=Path, default=ROOT / "device-reports")

    p_drive = sub.add_parser("drive", help="驱动一个视觉槽位并抓图，产出 pending.json")
    add_common(p_drive)
    p_drive.add_argument("--slot", required=True, choices=sorted(VISION_SLOTS))
    p_drive.add_argument("--sysbotbase-commit", required=True,
                         help="测试机 sys-botbase 的 commit（审计留痕，见 AGENTS.md）")
    p_drive.add_argument("--scenario", type=Path, default=None,
                         help="到达危险确认屏的按键脚本（JSON，格式同 input_smoke_harness）")
    p_drive.add_argument("--max-frames", type=int, default=12)
    p_drive.add_argument("--interval", type=float, default=1.2)
    p_drive.set_defaults(func=run_drive)

    p_submit = sub.add_parser("submit", help="按已封存判定提交设备观察 UI")
    add_common(p_submit)
    p_submit.add_argument("--run-id", required=True)
    p_submit.set_defaults(func=run_submit)
    return parser


def main(argv: list[str] | None = None) -> int:
    configure_console()
    print(smoke.LAB_ONLY_BANNER)
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (DriveError, sbb.SysBotBaseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())



