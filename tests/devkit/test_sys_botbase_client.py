#!/usr/bin/env python3
"""Host regression for tools/sys_botbase_client.py.

Drives the client against an in-process fake sys-botbase TCP server (no real
device) and verifies the exact wire strings, especially that a "physical hold"
is expressed as `clickSeq +A,W<ms>,-A` and a combo hold releases in reverse
order -- the shapes the overlay's danger-confirm gate distinguishes from touch.
"""
from __future__ import annotations

import socket
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import sys_botbase_client as sbb  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


class FakeSysBotBase:
    """Minimal fake: records every newline-terminated command, and answers the
    handful of data commands with canned replies."""

    REPLIES = {
        "getTitleID": "0100000000010000",
        "getSystemLanguage": "en-US",
        "peek": "0011223344",
        "pixelPeek": "ffd8ffe000",
    }

    def __init__(self) -> None:
        self.commands: list[str] = []
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


def main() -> int:
    test_sequence_builders()
    test_input_and_queries()
    if FAILURES:
        print(f"{FAILURES} sys_botbase_client test(s) failed", file=sys.stderr)
        return 1
    print("PASS: sys_botbase_client wire protocol and sequence builders")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

