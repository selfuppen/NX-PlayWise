from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import time
import os
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.automation.eden_runner import (
    GateError, McpClient, SCENARIOS, Suite, decode, healthy_exit, healthy_launch,
    receipt, save, seal, validate_observation, verify, profile_settings, set_profile_timezone,
    open_shared_read, read_observation_file,
)


def rejected(function) -> None:
    try:
        function()
    except (GateError, OSError, ValueError):
        return
    raise AssertionError("unsafe input accepted")


def test_health_gates() -> None:
    good = {"running": True, "pid": 123, "backend": "eden-control",
            "launch": {"phase": "game_ready", "target_reached": True}}
    healthy_launch(good)
    for field, value in [("running", False), ("backend", "windows-desktop"), ("pid", None)]:
        rejected(lambda: healthy_launch({**good, field: value}))
    rejected(lambda: healthy_launch({**good, "launch": {"phase": "exited", "target_reached": False}}))
    reply = {"stopped": True, "pid": 123, "return_code": 0}
    healthy_exit(123, reply, {"query_ok": True, "crashes": [{"pid": 999}]})
    rejected(lambda: healthy_exit(123, reply, {"query_ok": True, "crashes": [{"pid": 123}]}))
    rejected(lambda: healthy_exit(123, reply, {"query_ok": False, "crashes": []}))


def test_ui_and_receipt_identity() -> None:
    good = {"schema_version": 1, "release_id": "eden-test", "instance": "a12", "sequence": 1, "ui": {"view": 0}}
    validate_observation(good, "eden-test")
    rejected(lambda: validate_observation(good, "new-build"))
    rejected(lambda: validate_observation({**good, "ui": {"pin_text": "123"}}, "eden-test"))
    rejected(lambda: decode('{"request_id":"one","request_id":"two"}'))
    rejected(lambda: decode('{"tick":NaN}'))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "request-1.json"
        save(path, {"request_id": "request-1", "type": "offline_code", "status": "ok"})
        assert receipt(path, "offline_code", set())["status"] == "ok"
        rejected(lambda: receipt(path, "offline_code", {"request-1"}))
        rejected(lambda: receipt(path, "preview_offline_code", set()))


def test_failed_start_never_injects() -> None:
    class FakeClient:
        def __init__(self):
            self.names = []

        def tool(self, name, arguments=None):
            self.names.append(name)
            return {"running": False, "backend": "windows-desktop", "launch": {"target_reached": False}}

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        client = FakeClient()
        suite = Suite(client, root, root, {"manifest": {"release_id": "test"}}, 60)
        rejected(lambda: suite.action(button="A"))
        assert client.names == ["eden_status"]
        suite.deadline = 0
        rejected(lambda: suite.action(button="A"))
        assert client.names == ["eden_status"]


def test_stdio_and_tool_error() -> None:
    # Real pipe transport with deterministic fake server: a valid JSON-RPC
    # response can report a failed launch or tool rejection without pipe errors.
    script = """import sys,json
for line in sys.stdin:
 r=json.loads(line)
 if 'id' not in r: continue
 if r['method']=='initialize': result={}
 else: result={'isError':True,'content':[{'type':'text','text':'explicit rejection'}]}
 print(json.dumps({'jsonrpc':'2.0','id':r['id'],'result':result}),flush=True)
"""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        client = McpClient([sys.executable, "-u", "-c", script], root / "stderr.log", root)
        try:
            rejected(lambda: client.tool("eden_press_button", {"button": "A"}))
            assert len(list((root / "calls").glob("*.json"))) == 2
        finally:
            client.close()


def test_snapshot_replace_contention() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {"manifest": {"release_id": "test"}}, 60)
        value = {"schema_version": 1, "release_id": "test", "instance": "abc", "sequence": 1, "ui": {}}
        save(suite.app / "support/automation-ui.json", value)
        with patch("tools.automation.eden_runner.read_observation_file", side_effect=[PermissionError("replace"), (value, time.time())]) as reader:
            assert suite.observe() == value and reader.call_count == 2
        with patch("tools.automation.eden_runner.read_observation_file", side_effect=PermissionError("persistent")), \
             patch("tools.automation.eden_runner.time.monotonic", side_effect=[0, 0, 2]):
            rejected(suite.observe)


def test_reader_allows_emulator_delete_and_replace() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "snapshot.json"
        save(path, {"sequence": 1})
        replacement = path.with_suffix(".write")
        save(replacement, {"sequence": 2})
        with open_shared_read(path) as previous:
            # The emulator's SD writer removes the old name before renaming
            # its .write file. Sharing DELETE lets that remove succeed; the
            # retired file remains readable until this short handle closes.
            os.remove(path)
            assert json.loads(previous.read()) == {"sequence": 1}
        os.replace(replacement, path)
        value, modified = read_observation_file(path)
        assert value == {"sequence": 2} and abs(modified - time.time()) < 10


def test_pin_cancel_requires_unchanged_storage() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        path = suite.app / "auth.json"
        save(path, {"failed_attempts": 0})
        suite.pin_open = lambda: None
        suite.wait_ui = lambda **_: None
        suite.action = lambda **_: path.write_text('{"failed_attempts":0}', encoding="utf-8")
        rejected(suite.pin_cancel)  # Identical values still do not prove identical bytes.


def test_chinese_profile_preserved() -> None:
    source = "backend\\default=false\nbackend=3\nlanguage_index\\default=false\nlanguage_index=15\nregion_index\\default=false\nregion_index=4\ntime_zone_index\\default=true\ntime_zone_index=0\n"
    assert set_profile_timezone(source, None) == source
    value = profile_settings(set_profile_timezone(source, 34))
    assert value == {"backend": 3, "backend_default": False, "language_index": 15,
                     "language_index_default": False, "region_index": 4, "region_index_default": False,
                     "time_zone_index": 34, "time_zone_index_default": False}
    rejected(lambda: set_profile_timezone(source, -1))
    rejected(lambda: profile_settings(source.replace("language_index=15", "language_index=unknown")))


def test_actual_profile_paths_gate_input() -> None:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        manifest = {"release_id": "test"}
        class FakeClient:
            def __init__(self):
                self.calls = []
                self.profile = root / "user"

            def tool(self, name, arguments=None, timeout=30):
                self.calls.append(name)
                return {"running": True, "pid": 123, "backend": "eden-control",
                        "effective_profile_root": str(self.profile),
                        "launch": {"target_reached": True, "phase": "game_ready"}}
        client = FakeClient()
        suite = Suite(client, root, root, {"manifest": manifest}, 60)
        log = root / "user/log/eden_log.txt"
        log.parent.mkdir(parents=True)
        suite.wait_ui = lambda **_: {}
        captured = []
        suite.capture = captured.append
        save(suite.app / "build.json", manifest)
        for key in ("ConfigDir", "SDMCDir"):
            lines = {"ConfigDir": root / "user/config", "SDMCDir": root / "user/sdmc"}
            lines[key] = root / "operator/user"
            log.write_text("\n".join(f"LogSettings: {k}: {v}" for k, v in lines.items()), encoding="utf-8")
            rejected(suite.start)
            assert not captured and set(client.calls) == {"eden_start_game"}
        log.write_text(f"LogSettings: ConfigDir: {root / 'user/config'}\nLogSettings: SDMCDir: {root / 'user/sdmc'}\n", encoding="utf-8")
        client.profile = root / "operator/user"
        rejected(suite.start)
        assert not captured
        client.profile = root / "user"
        suite.start()
        assert captured == ["startup-123"]


def test_report_roundtrip_and_tamper() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        save(root / "identity.json", {"schema_version": 1})
        summary = {"schema_version": 1, "overall": "partial", "errors": ["fixture failure"],
                   "health": [], "scenarios": {name: {"status": "not_executed"} for name in SCENARIOS}}
        seal(root, summary)
        assert verify(root) == summary == verify(root)
        (root / "identity.json").write_text('{}', encoding="utf-8")
        rejected(lambda: verify(root))
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        save(root / "evidence-manifest.json", {"schema_version": 1, "files": [{"path": "../outside"}]})
        rejected(lambda: verify(root))


def main() -> None:
    test_health_gates()
    test_ui_and_receipt_identity()
    test_failed_start_never_injects()
    test_stdio_and_tool_error()
    test_snapshot_replace_contention()
    test_reader_allows_emulator_delete_and_replace()
    test_pin_cancel_requires_unchanged_storage()
    test_chinese_profile_preserved()
    test_actual_profile_paths_gate_input()
    test_report_roundtrip_and_tamper()
    print("PASS: Eden input/exit gates, stdio failures, identity and evidence roundtrip")


if __name__ == "__main__":
    main()
