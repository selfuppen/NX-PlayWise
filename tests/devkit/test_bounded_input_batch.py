#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from bounded_input_batch import BoundedInputBatch


class FakeClient:
    def __init__(self, fail_press: bool = False) -> None:
        self.commands: list[str] = []
        self.fail_press = fail_press

    def press(self, button: str) -> None:
        self.commands.append("press " + button)
        if self.fail_press:
            self.fail_press = False
            raise OSError("ambiguous send")

    def release(self, button: str) -> None:
        self.commands.append("release " + button)

    def raw(self, command: str) -> None:
        self.commands.append(command)

    def get_system_language(self) -> str:
        self.commands.append("getSystemLanguage")
        return "15"

    def capture_screenshot(self, path: Path) -> None:
        path.write_bytes(b"\xff\xd8fake\xff\xd9")


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        client = FakeClient()
        with BoundedInputBatch(client, root / "success", health=lambda: None,
                               sleep=lambda _: None) as batch:
            batch.hold(["ZR", "MINUS"])
            batch.capture("channel-test")
        assert client.commands[:5] == ["press ZR", "press MINUS", "release MINUS",
                                       "release ZR", "getSystemLanguage"]
        assert "detachController" in client.commands
        assert json.loads((root / "success/summary.json").read_text())["manual_observations"] is None

        client = FakeClient(fail_press=True)
        try:
            with BoundedInputBatch(client, root / "send-failure", health=lambda: None,
                                   sleep=lambda _: None) as batch:
                batch.hold(["Y"])
        except OSError:
            pass
        else:
            raise AssertionError("failure did not stop input")
        assert client.commands.count("press Y") == 1
        assert "release Y" in client.commands and "detachController" in client.commands

        client = FakeClient()
        with BoundedInputBatch(client, root / "private", health=lambda: None,
                               sleep=lambda _: None) as batch:
            with batch.sensitive_input():
                batch.hold(["DRIGHT"])
                batch.hold(["X"])
        journal = (root / "private/events.jsonl").read_text()
        assert "DRIGHT" not in journal and '"X"' not in journal
        assert "hold_ms" not in journal and "sensitive_input_finished" in journal

        client = FakeClient()
        try:
            with BoundedInputBatch(client, root / "limit", health=lambda: None,
                                   max_actions=1, sleep=lambda _: None) as batch:
                batch.hold(["Y"])
                batch.hold(["A"])
        except RuntimeError:
            pass
        else:
            raise AssertionError("action ceiling ignored")
        assert "press A" not in client.commands

        client = FakeClient()
        healthy = True
        def health() -> None:
            if not healthy:
                raise RuntimeError("device changed")
        with BoundedInputBatch(client, root / "fatal", health=health,
                               sleep=lambda _: None) as batch:
            batch.hold(["B"])
            healthy = False
        assert "detachController" not in client.commands
        assert json.loads((root / "fatal/summary.json").read_text())["cleanup"] == "unknown_stop_device_operations"

        client = FakeClient()
        with BoundedInputBatch(client, root / "readonly", health=lambda: None) as batch:
            batch.capture("before")
        assert "detachController" not in client.commands

        client = FakeClient()
        now = [0.0]
        try:
            with BoundedInputBatch(client, root / "deadline", health=lambda: None,
                                   clock=lambda: now[0], duration_s=1) as batch:
                now[0] = 2.0
                batch.hold(["A"])
        except TimeoutError:
            pass
        else:
            raise AssertionError("deadline ignored")
        assert client.commands == []
    print("PASS: bounded input, no replay, secret suppression, fatal stop and cleanup")


if __name__ == "__main__":
    main()
