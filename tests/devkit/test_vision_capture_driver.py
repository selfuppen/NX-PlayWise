#!/usr/bin/env python3
"""Host regression for tools/vision_capture_driver.py (no real device).

Exercises the testable core: the sampling-window capture loop keyed on
session.json state, pending.json writing with wire-log + screenshot hashes, the
observation-UI button choreography, the state-wait timeout, and a fake-driven
drive_slot that captures a baseline plus frames and holds A for >= 2.0s.
"""
from __future__ import annotations

import itertools
import json
import sys
import tempfile
from pathlib import Path

DEVKIT = Path(__file__).resolve().parent
sys.path.insert(0, str(DEVKIT))
sys.path.insert(0, str(DEVKIT.parents[1] / "tools"))

import vision_capture_driver as vcd  # noqa: E402
from test_sys_botbase_client import FakeSysBotBase  # noqa: E402
import sys_botbase_client as sbb  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


class ScriptedStateSource(vcd.SessionStateSource):
    """Returns states in order, sticking on the last once exhausted."""

    def __init__(self, states: list[dict]) -> None:
        self._states = list(states)
        self.reads = 0

    def read(self) -> dict:
        self.reads += 1
        if not self._states:
            return {}
        if len(self._states) == 1:
            return dict(self._states[0])
        return dict(self._states.pop(0))


def _counter_clock():
    counter = itertools.count()
    return lambda: float(next(counter))


def _noop_sleep(_seconds) -> None:
    return None


def test_observation_steps() -> None:
    steps = vcd.observation_steps("restriction_visible", "paused_or_suspended")
    wire = [(s.kind, s.args.get("button")) for s in steps]
    check(wire == [("click", "A"), ("click", "DDOWN"), ("click", "A")],
          "restriction_visible(0)+paused(1): A, then one DDOWN, then A")
    steps = vcd.observation_steps("no_visible_restriction", "continued")
    wire = [(s.kind, s.args.get("button")) for s in steps]
    check(wire == [("click", "DDOWN"), ("click", "A"), ("click", "A")],
          "no_visible_restriction(1)+continued(0): one DDOWN, A, A")


def test_capture_sampling_frames_caps_at_max() -> None:
    with FakeSysBotBase() as fake, tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        run_dir = reports / "vision" / "pause-a"
        run_dir.mkdir(parents=True)
        source = ScriptedStateSource([{"state": "sampling"}])
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            shots = vcd.capture_sampling_frames(client, source, run_dir, reports, max_frames=3,
                                                sleep=_noop_sleep, clock=_counter_clock(),
                                                now=lambda: "t")
        check(len(shots) == 3, "capture stops at max_frames while state stays sampling")
        for shot in shots:
            abs_path = reports / shot["path"]
            check(abs_path.is_file(), f"captured frame exists: {shot['path']}")
            check(vcd.sha256_file(abs_path) == shot["sha256"], "frame record carries its true sha256")


def test_capture_stops_when_leaving_sampling() -> None:
    with FakeSysBotBase() as fake, tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        run_dir = reports / "vision" / "pause-a"
        run_dir.mkdir(parents=True)
        source = ScriptedStateSource([{"state": "sampling"}, {"state": "sampling"},
                                      {"state": "awaiting_observation"}])
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            shots = vcd.capture_sampling_frames(client, source, run_dir, reports, max_frames=10,
                                                sleep=_noop_sleep, clock=_counter_clock(),
                                                now=lambda: "t")
        check(len(shots) == 2, "capture stops once the device leaves the sampling window")


def test_wait_for_state_timeout() -> None:
    source = ScriptedStateSource([{"state": "idle"}])
    raised = False
    try:
        vcd.wait_for_state(source, lambda s: s.get("state") == "sampling", timeout_s=2.0,
                           poll_s=1.0, sleep=_noop_sleep, clock=_counter_clock())
    except vcd.DriveError:
        raised = True
    check(raised, "wait_for_state raises DriveError when the target state never arrives")


def test_drive_slot_produces_pending_bundle() -> None:
    with FakeSysBotBase() as fake, tempfile.TemporaryDirectory() as tmp:
        reports = Path(tmp)
        plan = vcd.VISION_SLOTS["pause_on_game_a"]
        source = ScriptedStateSource([{"state": "sampling", "run_id": "pause-a",
                                       "campaign_id": "campaign-test"}])
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            result = vcd.drive_slot(client, source, plan, reports, nav_steps=[],
                                    sysbotbase_commit="ae13548", max_frames=3,
                                    sleep=_noop_sleep, clock=_counter_clock(), now=lambda: "t")
        pending = json.loads(Path(result["pending"]).read_text(encoding="utf-8"))
        check(pending["run_id"] == "pause-a", "pending carries the device run_id")
        check(pending["sysbotbase_commit"] == "ae13548", "pending records the sys-botbase commit")
        check(pending["slot"] == "pause_on_game_a", "pending records the slot")
        check(len(pending["screenshots"]) == 4, "baseline + 3 sampling frames are recorded")
        check(pending["screenshots"][0]["checkpoint"] == "baseline", "first frame is the baseline")
        check((reports / pending["wire_log"]).is_file(), "wire log is written")
        check("clickSeq +A,W2300,-A" in fake.commands, "danger confirm holds A for >= 2.0s")
        check("clickSeq +L,+DDOWN,+RSTICK,W400,-RSTICK,-DDOWN,-L" in fake.commands,
              "overlay is opened with the launch combo")
        check("click B" in fake.commands, "overlay is closed before sampling")
        # The sidecar can then be sealed from this very bundle.
        import vision_verdict as vv  # noqa: E402
        verdict = vv.seal(reports, Path(result["pending"]), observation="restriction_visible",
                          runtime_effect="paused_or_suspended", context_confirmed=True,
                          confidence="high", reasoning="frames show the prompt and a suspended game")
        check(verdict["run_id"] == "pause-a", "the pending bundle seals cleanly")


def main() -> int:
    test_observation_steps()
    test_capture_sampling_frames_caps_at_max()
    test_capture_stops_when_leaving_sampling()
    test_wait_for_state_timeout()
    test_drive_slot_produces_pending_bundle()
    if FAILURES:
        print(f"{FAILURES} vision_capture_driver test(s) failed", file=sys.stderr)
        return 1
    print("PASS: vision_capture_driver capture loop, choreography and pending bundle")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
