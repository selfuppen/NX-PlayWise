#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.device.bounded_input_batch import BoundedInputBatch
from tools.device.input_connection import prepare_controller_connection
from tests.devkit.test_bounded_input_batch import FakeClient


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        client = FakeClient()
        now = [0.0]
        with BoundedInputBatch(client, root / "success", health=lambda: None,
                               clock=lambda: now[0], sleep=lambda s: now.__setitem__(0, now[0] + s)) as batch:
            prepare_controller_connection(batch)
            assert client.commands == ["release A", "getSystemLanguage"] and now[0] == 1.2
            with batch.sensitive_input():
                batch.hold(["DRIGHT"], hold_ms=650)
            try:
                prepare_controller_connection(batch)
            except RuntimeError:
                pass
            else:
                raise AssertionError("second preparation accepted")
        assert client.commands.count("press DRIGHT") == 1 and client.commands.count("detachController") == 1
        journal = (root / "success/events.jsonl").read_text(encoding="utf-8")
        assert "DRIGHT" not in journal and "controller_connection_ready" in journal and '"buttons"' not in journal

        for fault in ("deadline", "send", "barrier", "fatal_after_wait", "invalid"):
            client = FakeClient()
            now = [0.0]
            healthy = [True]
            def health() -> None:
                if not healthy[0]:
                    raise RuntimeError("device changed")
            def sleep(s: float) -> None:
                now[0] += s
                if fault == "fatal_after_wait":
                    healthy[0] = False
            if fault == "send":
                def release(button: str) -> None:
                    client.commands.append("release " + button)
                    raise OSError("ambiguous neutral send")
                client.release = release
            if fault == "barrier":
                client.get_system_language = lambda: ""
            failed = False
            try:
                with BoundedInputBatch(client, root / fault, health=health, duration_s=1 if fault == "deadline" else 30,
                                       clock=lambda: now[0], sleep=sleep) as batch:
                    prepare_controller_connection(batch, settle_s=True if fault == "invalid" else 1.2)
                    batch.hold(["Y"])
            except (OSError, RuntimeError, TimeoutError, ValueError):
                failed = True
            assert failed and not any(c.startswith("press ") for c in client.commands), fault
            if fault in ("deadline", "invalid"):
                assert client.commands == [], fault
            elif fault == "fatal_after_wait":
                assert "detachController" not in client.commands
                assert json.loads((root / fault / "summary.json").read_text())["cleanup"] == "unknown_stop_device_operations"
            else:
                assert client.commands.count("release A") == 1 and client.commands.count("detachController") == 1, fault
        client = FakeClient()
        with BoundedInputBatch(client, root / "capture_only", health=lambda: None) as batch:
            batch.capture("readonly")
        assert client.commands == []
    print("PASS: opt-in neutral connection, lease/health stop, no replay, secret suppression and capture-only no input")


if __name__ == "__main__":
    main()
