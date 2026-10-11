#!/usr/bin/env python3
"""Minimal, dependency-free sys-botbase TCP client (lab-only).

sys-botbase (olliz0r, GPL-3.0) is a CFW sysmodule the operator installs on the
*test* Switch; it injects controller/touch input via hiddbg and reads screen/
memory over a socket. It is NEVER shipped in any PlayWise package and must never
run on a managed child's device -- it can drive the physical-hold gates the
product deliberately requires. This client only speaks the wire protocol; the
input smoke harness decides what to send and asserts outcomes from pulled SD
evidence.

Protocol notes (olliz0r/sys-botbase commands.md): commands are newline-
terminated ASCII; input commands (press/release/click/clickSeq/setStick/touch/
touchHold/configure) send no reply; data commands (getTitleID/peek/pixelPeek/
getSystemLanguage) reply with a single newline-terminated line (hex for binary).
"""
from __future__ import annotations

import argparse
import socket

DEFAULT_PORT = 6000

# Button tokens sys-botbase accepts (olliz0r util.c). Used only for client-side
# help/validation; the device is the authority on what it honours.
BUTTONS = frozenset({
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "PLUS", "MINUS",
    "DUP", "DDOWN", "DLEFT", "DRIGHT", "LSTICK", "RSTICK", "HOME", "CAPTURE",
})


class SysBotBaseError(RuntimeError):
    pass


def hold_sequence(button: str, hold_ms: int) -> str:
    """clickSeq fragment that presses a button, waits, then releases it."""
    return f"+{button},W{int(hold_ms)},-{button}"


def combo_hold_sequence(buttons: list[str], hold_ms: int) -> str:
    """clickSeq fragment holding several buttons at once for hold_ms, then
    releasing them in reverse order (e.g. ZL+ZR+A one-second confirm)."""
    press = ",".join(f"+{b}" for b in buttons)
    release = ",".join(f"-{b}" for b in reversed(buttons))
    return f"{press},W{int(hold_ms)},{release}"
class SysBotBaseClient:
    """One TCP connection to a sys-botbase instance. Use as a context manager."""

    def __init__(self, host: str, port: int = DEFAULT_PORT, timeout: float = 10.0) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: socket.socket | None = None
        self._buffer = b""

    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._buffer = b""

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def __enter__(self) -> "SysBotBaseClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def _send(self, command: str) -> None:
        if self._sock is None:
            raise SysBotBaseError("not connected")
        self._sock.sendall((command + "\n").encode("ascii"))

    def _read_line(self) -> str:
        if self._sock is None:
            raise SysBotBaseError("not connected")
        while b"\n" not in self._buffer:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise SysBotBaseError("connection closed while awaiting reply")
            self._buffer += chunk
        line, _, self._buffer = self._buffer.partition(b"\n")
        return line.decode("ascii", errors="replace").strip()

    def _query(self, command: str) -> str:
        self._send(command)
        return self._read_line()
    # --- input (no reply) ---
    def press(self, button: str) -> None:
        self._send(f"press {button}")

    def release(self, button: str) -> None:
        self._send(f"release {button}")

    def click(self, button: str) -> None:
        self._send(f"click {button}")

    def click_sequence(self, sequence: str) -> None:
        """Send a clickSeq (comma-separated: plain=click, +X press, -X release,
        Wn wait ms, %x,y left stick, &x,y right stick)."""
        self._send(f"clickSeq {sequence}")

    def set_stick(self, side: str, x: int, y: int) -> None:
        self._send(f"setStick {side} {x} {y}")

    def touch(self, points: list[tuple[int, int]]) -> None:
        coords = " ".join(f"{x} {y}" for x, y in points)
        self._send(f"touch {coords}")

    def touch_hold(self, x: int, y: int, hold_ms: int) -> None:
        self._send(f"touchHold {x} {y} {int(hold_ms)}")

    def configure(self, name: str, value: str) -> None:
        self._send(f"configure {name} {value}")

    # --- data (reply) ---
    def get_title_id(self) -> str:
        return self._query("getTitleID")

    def get_system_language(self) -> str:
        return self._query("getSystemLanguage")

    def peek(self, address: str, size: int) -> str:
        return self._query(f"peek {address} {int(size)}")

    def pixel_peek(self) -> bytes:
        """Best-effort screen capture; sys-botbase returns the JPEG as a hex
        line. Returns raw bytes (empty on a short/odd reply)."""
        line = self._query("pixelPeek")
        try:
            return bytes.fromhex(line)
        except ValueError:
            return b""

    def raw(self, command: str, expect_reply: bool = False) -> str | None:
        if expect_reply:
            return self._query(command)
        self._send(command)
        return None
def _main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Poke a sys-botbase instance (lab-only).")
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--timeout", type=float, default=10.0)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("title", help="print the foreground application title id")
    p_hold = sub.add_parser("hold", help="hold a button for N ms via clickSeq")
    p_hold.add_argument("button")
    p_hold.add_argument("--ms", type=int, default=1200)
    p_raw = sub.add_parser("raw", help="send a raw command")
    p_raw.add_argument("text")
    p_raw.add_argument("--reply", action="store_true")
    args = parser.parse_args(argv)
    with SysBotBaseClient(args.host, args.port, args.timeout) as client:
        if args.command == "title":
            print(client.get_title_id())
        elif args.command == "hold":
            client.click_sequence(hold_sequence(args.button, args.ms))
            print(f"held {args.button} for {args.ms}ms")
        elif args.command == "raw":
            reply = client.raw(args.text, expect_reply=args.reply)
            if reply is not None:
                print(reply)
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())

