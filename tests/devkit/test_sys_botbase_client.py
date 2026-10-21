#!/usr/bin/env python3
"""Host regression for tools/automation/sys_botbase_client.py.

Drives the client against an in-process fake sys-botbase TCP server (no real
device) and verifies the exact wire strings, especially that a "physical hold"
is expressed as `clickSeq +A,W<ms>,-A` and a combo hold releases in reverse
order -- the shapes the overlay's danger-confirm gate distinguishes from touch.
"""
from __future__ import annotations

import socket
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import tools.automation.sys_botbase_client as sbb  # noqa: E402

FAILURES = 0

# A minimal but structurally real JFIF image (SOI + APP0 + EOI). pixelPeek
# returns the JPEG as an uppercase hex line; capture_screenshot only trusts the
# SOI marker, so this stands in for a real device frame in the host tests.
MINIMAL_JPEG_HEX = "FFD8FFE000104A46494600010100000100010000FFD9"


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


class FakeSysBotBase:
    """Minimal fake: records every newline-terminated command, and answers the
    handful of data commands with canned replies. `pixel_reply` is configurable
    so a test can simulate the empty capssc/debug-flag failure (pixel_reply="")."""

    BASE_REPLIES = {
        "getTitleID": "0100000000010000",
        "getSystemLanguage": "15",
        "peek": "0011223344",
    }

    def __init__(self, pixel_reply: str = MINIMAL_JPEG_HEX) -> None:
        self.commands: list[str] = []
        self.REPLIES = dict(self.BASE_REPLIES)
        self.REPLIES["pixelPeek"] = pixel_reply
        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind(("127.0.0.1", 0))
        self._server.listen(1)
        self.port = self._server.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self) -> "FakeSysBotBase":
        self._thread.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        try:
            self._server.close()
        except OSError:
            pass
        self._thread.join(timeout=2.0)
    def _serve(self) -> None:
        try:
            conn, _ = self._server.accept()
        except OSError:
            return
        buffer = b""
        with conn:
            while True:
                try:
                    chunk = conn.recv(4096)
                except OSError:
                    return
                if not chunk:
                    return
                buffer += chunk
                while b"\n" in buffer:
                    line, _, buffer = buffer.partition(b"\n")
                    command = line.decode("ascii", errors="replace").strip()
                    if not command:
                        continue
                    self.commands.append(command)
                    token = command.split()[0]
                    if token in self.REPLIES:
                        conn.sendall((self.REPLIES[token] + "\n").encode("ascii"))


def test_sequence_builders() -> None:
    check(sbb.hold_sequence("A", 1200) == "+A,W1200,-A", "hold_sequence presses, waits, releases")
    check(sbb.combo_hold_sequence(["ZL", "ZR", "A"], 1000) == "+ZL,+ZR,+A,W1000,-A,-ZR,-ZL",
          "combo_hold_sequence releases in reverse order")


def test_input_and_queries() -> None:
    with FakeSysBotBase() as fake:
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            check(client.get_title_id() == "0100000000010000", "get_title_id returns the server reply")
            client.press("A")
            client.release("A")
            client.click("B")
            client.click_sequence(sbb.hold_sequence("A", 1200))
            client.touch([(640, 360)])
            client.touch_hold(100, 200, 500)
            check(client.peek("0x1000", 5) == "0011223344", "peek returns hex reply")
    check(fake.commands[0] == "getTitleID", "first command is the title query")
    check("press A" in fake.commands, "press encodes as 'press A'")
    check("release A" in fake.commands, "release encodes as 'release A'")
    check("click B" in fake.commands, "click encodes as 'click B'")
    check("clickSeq +A,W1200,-A" in fake.commands, "hold is sent as a clickSeq long-press")
    check("touch 640 360" in fake.commands, "touch encodes coordinates")
    check("touchHold 100 200 500" in fake.commands, "touchHold encodes coordinates and duration")
    check("peek 0x1000 5" in fake.commands, "peek encodes address and size")


def test_capture_screenshot() -> None:
    with FakeSysBotBase() as fake:
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / "sub" / "shot.jpg"
                written = client.capture_screenshot(out)
                check(out.is_file() and written > 0, "capture_screenshot writes the JPEG")
                check(out.read_bytes()[:2] == b"\xff\xd8", "captured file starts with JPEG SOI")
    check("pixelPeek" in fake.commands, "capture_screenshot sends pixelPeek")
    # An empty pixelPeek (capssc 0x668CE on an unpatched debug flag) must raise
    # loudly and write nothing, so the pipeline fails before any PCTL mutation.
    with FakeSysBotBase(pixel_reply="") as fake:
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / "shot.jpg"
                raised = False
                try:
                    client.capture_screenshot(out)
                except sbb.SysBotBaseError:
                    raised = True
                check(raised, "empty pixelPeek must raise SysBotBaseError")
                check(not out.exists(), "no file is written when the capture is empty")


def test_typed_reply_guards() -> None:
    for command, method, bad in (
        ("getTitleID", "get_title_id", "hiddbgSetHdlsState: 123"),
        ("getTitleID", "get_title_id", "010000000001000"),
        ("getTitleID", "get_title_id", "010000000001000G"),
        *[("getSystemLanguage", "get_system_language", reply) for reply in
          ("hiddbgAttachHdlsVirtualDevice: 123", "", "en-US", "-1", "+15", "015", "2147483648")],
    ):
        with FakeSysBotBase() as fake:
            # A pending valid line must not be consumed as a new query's reply.
            fake.REPLIES[command] = bad + "\n15"
            with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
                try:
                    getattr(client, method)()
                except sbb.SysBotBaseError:
                    pass
                else:
                    check(False, "malformed typed reply rejected: " + repr(bad))
                check(client._sock is None, "invalid reply closes the unsynchronized connection")
                try:
                    client.press("Y")
                except sbb.SysBotBaseError:
                    pass
                else:
                    check(False, "closed invalid connection cannot send another input")
        check(fake.commands == [command], "invalid reply never retries or sends input")
    with FakeSysBotBase() as fake:
        with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
            check(client.get_system_language() == "15", "numeric language reply remains a string")


def test_home_title_reply_and_update_foreground() -> None:
    from tools.device.auto_update import Updater, UpdateError
    with FakeSysBotBase() as fake:
        fake.REPLIES['getTitleID'] = ''  # Upstream emits a bare newline when no main NSO is present.
        with sbb.SysBotBaseClient('127.0.0.1', fake.port, timeout=3.0) as client:
            check(client.get_title_id() == '', 'valid no-application title remains empty')
            updater = Updater.__new__(Updater)
            updater.bot = client
            updater.guard = lambda: None  # No device or FTP; exercise the actual foreground check.
            updater.foreground('')
            try:
                updater.foreground('0100000000010000')
            except UpdateError:
                pass
            else:
                check(False, 'empty HOME reply cannot satisfy an application foreground check')
            fake.REPLIES['getTitleID'] = '0100000000010000'
            updater.foreground('0100000000010000')
            check(client.get_system_language() == '15', 'valid empty title preserves the query connection')
    check(fake.commands == ['getTitleID'] * 4 + ['getSystemLanguage'],
          'HOME/application queries require no retries, input or reconnection')


def test_bad_barrier_stops_bounded_input() -> None:
    import json
    from tools.device.bounded_input_batch import BoundedInputBatch
    with tempfile.TemporaryDirectory() as tmp, FakeSysBotBase() as fake:
        fake.REPLIES["getSystemLanguage"] = "hiddbgSetHdlsState: 123\n15"
        out = Path(tmp) / "batch"
        try:
            with sbb.SysBotBaseClient("127.0.0.1", fake.port, timeout=3.0) as client:
                with BoundedInputBatch(client, out, health=lambda: None, max_actions=1,
                                       sleep=lambda _: None) as batch:
                    batch.hold(["Y"])
        except sbb.SysBotBaseError:
            pass
        else:
            check(False, "diagnostic output cannot satisfy an input wire barrier")
        value = json.loads((out / "summary.json").read_bytes())
        check(value["failed"] and value["cleanup"] == "unknown_stop_device_operations",
              "unsynchronized socket retains unknown cleanup without reinitializing HID")
    check(fake.commands == ["press Y", "release Y", "getSystemLanguage"],
          "bad barrier follows release and sends no further input or detach")


def main() -> int:
    test_sequence_builders()
    test_input_and_queries()
    test_capture_screenshot()
    test_typed_reply_guards()
    test_home_title_reply_and_update_foreground()
    test_bad_barrier_stops_bounded_input()
    if FAILURES:
        print(f"{FAILURES} sys_botbase_client test(s) failed", file=sys.stderr)
        return 1
    print("PASS: sys_botbase_client wire protocol and sequence builders")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

