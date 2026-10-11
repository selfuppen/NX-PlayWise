#!/usr/bin/env python3
"""Host regression for tools/capability_probe.py.

Drives the orchestrator with in-memory fakes (fake sys-botbase, fake wire, a
session source that models the device advancing its own phases) so no real
device or real time is needed. Pins: preflight hard-fails (dead lab, bad
pixelPeek) and passes; probe_ftp_port falls back across ports; run_restriction
captures a baseline + frames and NEVER submits an observation or seals; and
run_timer_ab drives the 7 activation phases in order to completion.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import capability_probe as cp  # noqa: E402
import lab_wire_client as lwc  # noqa: E402
import sys_botbase_client as sbb  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


class FakeSbb:
    def __init__(self, title: str = "0100000000ABCD00", pixel_ok: bool = True,
                 language: str = "en-US") -> None:
        self.title = title
        self.pixel_ok = pixel_ok
        self.language = language
        self.captures = 0

    def get_system_language(self) -> str:
        return self.language

    def get_title_id(self) -> str:
        return self.title

    def capture_screenshot(self, path) -> int:
        if not self.pixel_ok:
            raise sbb.SysBotBaseError("empty pixelPeek (0x668CE)")
        self.captures += 1
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\xff\xd8\xff\xd9")
        return 4


class FakeFtp:
    """Only needs download_file + an endpoint for _pull_machine / preflight."""

    class _Endpoint:
        port = 21

    def __init__(self) -> None:
        self.endpoint = FakeFtp._Endpoint()
        self.reconnects = 0

    def reconnect(self) -> None:
        self.reconnects += 1

    def download_file(self, remote: str, local) -> int:
        local = Path(local)
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_text('{"ok": true}', encoding="utf-8")
        return 1


class FakeWire:
    """Models the device session.json via a shared mutable ``device`` dict and
    records every submitted request type so the test can prove no observation
    is ever submitted."""

    def __init__(self, device: dict, *, timeout_on=None, result_overrides=None) -> None:
        self.device = device
        self.submitted: list = []
        self.reset_calls = 0
        self._timeout_on = set(timeout_on or [])
        self._overrides = result_overrides or {}
        self._last = None

    def submit(self, type_: str, payload=None, *, request_id=None, created_at=None) -> str:
        self.submitted.append((type_, payload))
        self._last = type_
        if type_ == "lab_session_start":
            mode = (payload or {}).get("mode", "full")
            self.device.clear()
            self.device.update(mode=mode, state="ready", next_phase=0)
        elif type_ == "lab_phase_start":
            self.device["state"] = "sampling"
        return f"rid-{len(self.submitted)}"

    def wait_result(self, request_id, scratch, *, timeout_s=20.0, poll_s=1.0) -> dict:
        if self._last in self._timeout_on:
            raise lwc.WireTimeout(f"fake timeout {self._last}")
        if self._last in self._overrides:
            return self._overrides[self._last]
        return {"status": "ok", "error_code": None, "error_reason": None,
                "lab_session": dict(self.device), "raw": {}}

    def reset_session(self) -> None:
        self.reset_calls += 1
        self.device.clear()


class FakeSource:
    """Mirrors the device scheduler: a sampling phase lasts ``dwell`` reads, then
    advances to the next ready phase, or to complete/awaiting_observation."""

    def __init__(self, device: dict, *, phase_count: int = 7, dwell: int = 2,
                 end_awaiting: bool = False) -> None:
        self.device = device
        self.phase_count = phase_count
        self.dwell = dwell
        self.end_awaiting = end_awaiting
        self._poll = 0

    def read(self) -> dict:
        if self.device.get("state") == "sampling":
            self._poll += 1
            if self._poll >= self.dwell:
                self._poll = 0
                n = self.device.get("next_phase", 0)
                if self.end_awaiting or self.device.get("mode") == "restriction_quick":
                    self.device["state"] = "awaiting_observation"
                elif n + 1 >= self.phase_count:
                    self.device["state"] = "complete"
                else:
                    self.device["state"] = "ready"
                    self.device["next_phase"] = n + 1
        return dict(self.device)


# <!-- APPEND-MARKER -->

def _ctx(tmp: Path, device: dict, source, wire, sbb_client, *, dwell_sleep=None):
    return cp.ProbeContext(
        wire=wire, sbb=sbb_client, ftp=FakeFtp(), session_source=source,
        reports_dir=tmp / "reports", scratch=tmp / "scratch",
        result_timeout=5.0, sampling_timeout=5.0, phase_slack=5.0,
        sleep=lambda _s: None, clock=lambda: 0.0,
    )


def test_probe_ftp_port_falls_back() -> None:
    attempts = []

    class FlakyClient:
        def __init__(self, endpoint, timeout=10):
            self.endpoint = endpoint

        def connect(self):
            attempts.append(self.endpoint.port)
            if self.endpoint.port == 21:
                raise OSError("refused")

        def close(self):
            pass

    original = cp.ftp_installer.SwitchFtpClient
    cp.ftp_installer.SwitchFtpClient = FlakyClient
    try:
        endpoint = cp.probe_ftp_port("1.2.3.4", (21, 5000))
        check(endpoint.port == 5000, "probe_ftp_port falls back to 5000 when 21 refuses")
        check(attempts == [21, 5000], "both ports tried in order")
        raised = False
        try:
            cp.probe_ftp_port("1.2.3.4", (21,))
        except cp.ProbeError:
            raised = True
        check(raised, "all-refused raises ProbeError")
    finally:
        cp.ftp_installer.SwitchFtpClient = original


def test_preflight_passes_and_notes_home() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        device: dict = {}
        wire = FakeWire(device)
        source = FakeSource(device)
        ctx = _ctx(Path(tmp), device, source, wire, FakeSbb(title="0100000000ABCD00"))
        ctx.scratch.mkdir(parents=True, exist_ok=True)
        result = cp.preflight(ctx)
        check(result.lab_alive and result.pixelpeek_ok, "preflight passes with a live lab + pixelPeek")
        check(not result.foreground_is_home, "a game title is not flagged as HOME")
        # HOME foreground -> advisory note, still passes
        ctx2 = _ctx(Path(tmp), {}, FakeSource({}), FakeWire({}), FakeSbb(title=cp.HOME_TITLE_ID))
        ctx2.scratch.mkdir(parents=True, exist_ok=True)
        res2 = cp.preflight(ctx2)
        check(res2.foreground_is_home and res2.notes, "HOME foreground adds an advisory note")
        # Empty title (device sitting at HOME) is a note, not a hard failure.
        ctx3 = _ctx(Path(tmp), {}, FakeSource({}), FakeWire({}), FakeSbb(title=""))
        ctx3.scratch.mkdir(parents=True, exist_ok=True)
        res3 = cp.preflight(ctx3)
        check(res3.foreground_is_home, "empty title is treated as no-foreground-game, not a dead sys-botbase")


def test_preflight_fails_on_dead_sysbotbase() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        device: dict = {}
        ctx = _ctx(Path(tmp), device, FakeSource(device), FakeWire(device), FakeSbb(language=""))
        ctx.scratch.mkdir(parents=True, exist_ok=True)
        raised = False
        try:
            cp.preflight(ctx)
        except cp.ProbeError as exc:
            raised = "sys-botbase" in str(exc)
        check(raised, "an empty getSystemLanguage fails preflight as a dead sys-botbase")


def test_preflight_fails_on_dead_lab_and_bad_pixelpeek() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        device: dict = {}
        wire = FakeWire(device, timeout_on={"lab_session_status"})
        ctx = _ctx(Path(tmp), device, FakeSource(device), wire, FakeSbb())
        ctx.scratch.mkdir(parents=True, exist_ok=True)
        raised = False
        try:
            cp.preflight(ctx)
        except cp.ProbeError as exc:
            raised = "lab" in str(exc) or "Lab" in str(exc)
        check(raised, "lab status timeout fails preflight with a switch-to-lab hint")
        # bad pixelPeek
        device2: dict = {}
        ctx2 = _ctx(Path(tmp), device2, FakeSource(device2), FakeWire(device2),
                    FakeSbb(pixel_ok=False))
        ctx2.scratch.mkdir(parents=True, exist_ok=True)
        raised2 = False
        try:
            cp.preflight(ctx2)
        except cp.ProbeError as exc:
            raised2 = "pixelPeek" in str(exc)
        check(raised2, "unusable pixelPeek fails preflight with a debug-flag hint")


def test_run_restriction_captures_without_observation() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        device: dict = {}
        wire = FakeWire(device)
        source = FakeSource(device, dwell=4, end_awaiting=True)
        ctx = _ctx(Path(tmp), device, source, wire, FakeSbb())
        ctx.scratch.mkdir(parents=True, exist_ok=True)
        record = cp.run_restriction(ctx)
        submitted = [t for t, _ in wire.submitted]
        check(wire.reset_calls == 1, "restriction resets the session once up front")
        check(submitted == ["lab_session_start", "lab_phase_start"],
              "restriction submits only session_start + phase_start")
        check("lab_observation" not in submitted, "restriction NEVER submits an observation")
        check(len(record["screenshots"]) >= 2, "a baseline plus >=1 sampling frame captured")
        check(record["screenshots"][0]["checkpoint"] == "baseline", "first shot is the baseline")
        check("lab/phase-5.json" in record["machine"], "restriction pulls phase-5 machine evidence")
        check((ctx.reports_dir / "restriction_quick" / "scenario.json").is_file(),
              "scenario.json written")
        check((ctx.reports_dir / "restriction_quick" / "wire.log").is_file(), "wire.log written")


def test_run_timer_ab_drives_seven_phases() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        device: dict = {}
        wire = FakeWire(device)
        source = FakeSource(device, phase_count=7, dwell=2)
        ctx = _ctx(Path(tmp), device, source, wire, FakeSbb())
        ctx.scratch.mkdir(parents=True, exist_ok=True)
        record = cp.run_timer_ab(ctx, sleep_checkpoint=False)
        check(record["phases_run"] == list(lwc.LAB_ACTIVATION_AB_PHASES),
              "timer_ab drives the 7 AB phases in exact order")
        phase_starts = [p for t, p in wire.submitted if t == "lab_phase_start"]
        check(len(phase_starts) == 7, "exactly 7 phase_start requests")
        check("lab_observation" not in [t for t, _ in wire.submitted],
              "timer_ab NEVER submits an observation")
        check("lab/phase-0.json" in record["machine"] and "lab/public.json" in record["machine"],
              "timer_ab pulls per-phase + public machine evidence")


def test_run_timer_ab_sleep_checkpoint_reconnects() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        device: dict = {}
        wire = FakeWire(device)
        source = FakeSource(device, phase_count=7, dwell=2)
        ftp = FakeFtp()
        ctx = cp.ProbeContext(
            wire=wire, sbb=FakeSbb(), ftp=ftp, session_source=source,
            reports_dir=Path(tmp) / "reports", scratch=Path(tmp) / "scratch",
            result_timeout=5.0, sampling_timeout=5.0, phase_slack=5.0,
            interactive=False, sleep=lambda _s: None, clock=lambda: 0.0,
        )
        ctx.scratch.mkdir(parents=True, exist_ok=True)
        record = cp.run_timer_ab(ctx, sleep_checkpoint=True)
        check(record["phases_run"] == list(lwc.LAB_ACTIVATION_AB_PHASES),
              "sleep-checkpoint run still drives all 7 phases")
        check(ftp.reconnects == 1, "FTP reconnected exactly once, after the sleep leg")
        check(("lab_phase_start", {"phase": "ab_sleep_wake"}) in wire.submitted,
              "ab_sleep_wake is submitted (before the sleep/reconnect), not skipped")


def main() -> int:
    test_probe_ftp_port_falls_back()
    test_preflight_passes_and_notes_home()
    test_preflight_fails_on_dead_sysbotbase()
    test_preflight_fails_on_dead_lab_and_bad_pixelpeek()
    test_run_restriction_captures_without_observation()
    test_run_timer_ab_drives_seven_phases()
    test_run_timer_ab_sleep_checkpoint_reconnects()
    if FAILURES:
        print(f"{FAILURES} capability_probe test(s) failed", file=sys.stderr)
        return 1
    print("PASS: capability_probe preflight, restriction and timer_ab drive (capture-only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

