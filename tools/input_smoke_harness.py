#!/usr/bin/env python3
"""sys-botbase input-injection smoke harness (lab-only, ADVISORY).

Drives scripted controller/touch input at the *test* Switch through sys-botbase
and (optionally) pulls the SD evidence back over FTP to assert machine-checkable
outcomes. This is a regression aid, never authoritative: it can drive the very
physical-hold gates the product requires, so it must only ever run against a
dedicated test console with sys-botbase installed -- never a managed child's
device -- and the authoritative "physical presence works" evidence stays with a
human operator (真机兜底).

What it proves without a camera/OCR: that input reaches the system (foreground
title readable, evidence files change) and that the harness expresses the
"physical hold" vs "touch" distinction on the wire exactly as the danger-confirm
gate expects (held-A accepted, touch rejected). The on-device acceptance/
rejection itself is confirmed by a human on real hardware.
"""
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sys_botbase_client as sbb  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable

LAB_ONLY_BANNER = (
    "================================================================\n"
    " sys-botbase INPUT SMOKE -- LAB ONLY, ADVISORY, NEVER AUTHORITATIVE\n"
    " Only run against a dedicated test Switch with sys-botbase installed.\n"
    " It can drive the physical-hold safety gates; never point it at a\n"
    " managed child's device. Authoritative presence stays human-verified.\n"
    "================================================================"
)

# libtesla default launch combo (tesla.hpp): L + DPAD_DOWN + RStick press.
DEFAULT_LAUNCH_COMBO = ["L", "DDOWN", "RSTICK"]
@dataclass
class SmokeStep:
    kind: str
    args: dict = field(default_factory=dict)
    note: str = ""


def overlay_open_scenario(launch_combo: list[str] | None = None, hold_ms: int = 400) -> list[SmokeStep]:
    combo = launch_combo or DEFAULT_LAUNCH_COMBO
    return [
        SmokeStep("combo_hold", {"buttons": combo, "hold_ms": hold_ms}, "open Tesla overlay"),
        SmokeStep("wait", {"ms": 1500}, "let overlay draw and connect IPC"),
        SmokeStep("click", {"button": "B"}, "close overlay"),
    ]


def danger_hold_vs_touch_scenario(confirm_xy: tuple[int, int],
                                  nav_prefix: list[SmokeStep] | None = None,
                                  hold_ms: int = 1200) -> list[SmokeStep]:
    """Reach a dangerous-confirm screen (nav_prefix supplied by the operator,
    since it depends on current rules/state), then exercise both paths: a touch
    that the overlay must REJECT, and a >=1s physical hold of A it must ACCEPT."""
    x, y = confirm_xy
    steps = list(nav_prefix or [])
    steps += [
        SmokeStep("touch", {"points": [[x, y]]}, "touch dangerous confirm (overlay must REJECT)"),
        SmokeStep("wait", {"ms": 500}, ""),
        SmokeStep("hold", {"button": "A", "hold_ms": hold_ms}, "physical hold A (overlay must ACCEPT)"),
    ]
    return steps


def execute_step(client, step: SmokeStep, sleep=time.sleep) -> str:
    """Dispatch one step to the sys-botbase client. Returns a short label of the
    wire action taken (used by the report and by the host test)."""
    kind, a = step.kind, step.args
    if kind == "wait":
        sleep(a.get("ms", 0) / 1000.0)
        return f"wait {a.get('ms', 0)}ms"
    if kind == "hold":
        seq = sbb.hold_sequence(a["button"], a.get("hold_ms", 1000))
        client.click_sequence(seq)
        return f"clickSeq {seq}"
    if kind == "combo_hold":
        seq = sbb.combo_hold_sequence(a["buttons"], a.get("hold_ms", 1000))
        client.click_sequence(seq)
        return f"clickSeq {seq}"
    if kind == "click":
        client.click(a["button"])
        return f"click {a['button']}"
    if kind == "press":
        client.press(a["button"])
        return f"press {a['button']}"
    if kind == "release":
        client.release(a["button"])
        return f"release {a['button']}"
    if kind == "touch":
        points = [tuple(p) for p in a["points"]]
        client.touch(points)
        return "touch " + " ".join(f"{x} {y}" for x, y in points)
    if kind == "touch_hold":
        client.touch_hold(a["x"], a["y"], a.get("hold_ms", 1000))
        return f"touchHold {a['x']} {a['y']} {a.get('hold_ms', 1000)}"
    if kind == "seq":
        client.click_sequence(a["sequence"])
        return f"clickSeq {a['sequence']}"
    raise ValueError(f"unknown step kind: {kind}")


def run_scenario(client, steps: list[SmokeStep], sleep=time.sleep) -> list[str]:
    """Execute every step in order; returns the ordered wire-action labels."""
    return [execute_step(client, step, sleep=sleep) for step in steps]


def load_scenario(path: Path) -> list[SmokeStep]:
    data = json.loads(path.read_text(encoding="utf-8"))
    steps = data.get("steps", data) if isinstance(data, dict) else data
    return [SmokeStep(item["kind"], item.get("args", {}), item.get("note", "")) for item in steps]
def _pull_and_assert(url: str, reports: Path) -> int:
    pull = subprocess.run([PYTHON, str(ROOT / "tools" / "device_lab_roundtrip.py"),
                           "pull", "--url", url, "--reports", str(reports), "--apply"])
    if pull.returncode != 0:
        return pull.returncode
    return subprocess.run([PYTHON, str(ROOT / "tools" / "assert_device_evidence.py"),
                           "--evidence", str(reports / "standard")]).returncode


def main(argv: list[str] | None = None, client_factory=None, sleep=time.sleep) -> int:
    parser = argparse.ArgumentParser(description="sys-botbase input smoke (lab-only, advisory).")
    parser.add_argument("--host", required=True, help="sys-botbase host (test Switch IP)")
    parser.add_argument("--port", type=int, default=sbb.DEFAULT_PORT)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--scenario", type=Path, default=None,
                        help="JSON scenario file; default opens then closes the overlay")
    parser.add_argument("--launch-combo", default=",".join(DEFAULT_LAUNCH_COMBO),
                        help="comma-separated buttons for the overlay launch combo")
    parser.add_argument("--pull", action="store_true", help="pull SD evidence over FTP after input")
    parser.add_argument("--ftp-url", default=None, help="FTP url for --pull (e.g. ftp://192.168.0.123/)")
    parser.add_argument("--reports", type=Path, default=ROOT / "device-reports")
    args = parser.parse_args(argv)
    print(LAB_ONLY_BANNER)
    steps = load_scenario(args.scenario) if args.scenario \
        else overlay_open_scenario([b for b in args.launch_combo.split(",") if b])
    factory = client_factory or (lambda: sbb.SysBotBaseClient(args.host, args.port, args.timeout))
    client = factory()
    client.connect()
    try:
        print(f"foreground title id: {client.get_title_id()}")
        actions = run_scenario(client, steps, sleep=sleep)
    finally:
        client.close()
    print(f"sent {len(actions)} input action(s):")
    for action in actions:
        print(f"  -> {action}")
    if args.pull:
        if not args.ftp_url:
            print("--pull requires --ftp-url", file=sys.stderr)
            return 2
        return _pull_and_assert(args.ftp_url, args.reports)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

