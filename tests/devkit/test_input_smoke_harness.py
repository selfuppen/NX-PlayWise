#!/usr/bin/env python3
"""Host regression for tools/input_smoke_harness.py (no device, no FTP).

Injects a recording fake client into the harness and asserts the exact wire
actions a scenario produces -- in particular that the dangerous-confirm scenario
emits a touch (which the overlay must reject) and a >=1s physical hold of A
(which it must accept), in that order.
"""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import input_smoke_harness as harness  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


class RecordingClient:
    """Duck-typed stand-in for SysBotBaseClient that records method calls."""

    def __init__(self) -> None:
        self.calls: list[tuple] = []

    def connect(self) -> None:
        self.calls.append(("connect",))

    def close(self) -> None:
        self.calls.append(("close",))

    def get_title_id(self) -> str:
        self.calls.append(("get_title_id",))
        return "0100000000010000"

    def click(self, button: str) -> None:
        self.calls.append(("click", button))

    def press(self, button: str) -> None:
        self.calls.append(("press", button))

    def release(self, button: str) -> None:
        self.calls.append(("release", button))

    def click_sequence(self, sequence: str) -> None:
        self.calls.append(("click_sequence", sequence))

    def set_stick(self, side: str, x: int, y: int) -> None:
        self.calls.append(("set_stick", side, x, y))

    def touch(self, points) -> None:
        self.calls.append(("touch", tuple(points)))

    def touch_hold(self, x: int, y: int, hold_ms: int) -> None:
        self.calls.append(("touch_hold", x, y, hold_ms))
def test_overlay_open_scenario_actions() -> None:
    client = RecordingClient()
    actions = harness.run_scenario(client, harness.overlay_open_scenario(), sleep=lambda s: None)
    check(actions[0] == "clickSeq +L,+DDOWN,+RSTICK,W400,-RSTICK,-DDOWN,-L",
          "overlay-open sends the libtesla launch combo as a clickSeq")
    check(any(a.startswith("wait ") for a in actions), "overlay-open waits for the overlay to draw")
    check(actions[-1] == "click B", "overlay-open closes the overlay with B")


def test_danger_hold_vs_touch() -> None:
    client = RecordingClient()
    steps = harness.danger_hold_vs_touch_scenario((640, 360), hold_ms=1200)
    actions = harness.run_scenario(client, steps, sleep=lambda s: None)
    check("touch 640 360" in actions, "danger scenario sends a touch (overlay must REJECT)")
    check("clickSeq +A,W1200,-A" in actions, "danger scenario sends a 1.2s physical hold of A")
    check(actions.index("touch 640 360") < actions.index("clickSeq +A,W1200,-A"),
          "the rejected touch is attempted before the accepted physical hold")


def test_load_scenario_roundtrip() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "scenario.json"
        path.write_text(json.dumps({"steps": [{"kind": "click", "args": {"button": "Y"}, "note": "refresh"}]}),
                        encoding="utf-8")
        steps = harness.load_scenario(path)
    check(len(steps) == 1 and steps[0].kind == "click" and steps[0].args["button"] == "Y",
          "a JSON scenario file loads into steps")


def test_main_without_pull() -> None:
    client = RecordingClient()
    rc = harness.main(["--host", "192.0.2.1"], client_factory=lambda: client, sleep=lambda s: None)
    check(rc == 0, "an input-only run (no --pull) returns 0")
    kinds = [call[0] for call in client.calls]
    check(kinds[0] == "connect" and kinds[-1] == "close", "main connects then closes the client")
    check(("get_title_id",) in client.calls, "main reads the foreground title id before driving input")


def main() -> int:
    test_overlay_open_scenario_actions()
    test_danger_hold_vs_touch()
    test_load_scenario_roundtrip()
    test_main_without_pull()
    if FAILURES:
        print(f"{FAILURES} input_smoke_harness test(s) failed", file=sys.stderr)
        return 1
    print("PASS: input_smoke_harness scenarios and wire actions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

