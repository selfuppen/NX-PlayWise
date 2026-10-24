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
    GateError, ObservationPending, McpClient, SCENARIOS, Suite, decode, healthy_exit, healthy_launch,
    receipt, save, seal, validate_observation, verify, profile_settings, set_profile_timezone,
    open_shared_read, read_observation_file,
    scenario_names,
    verify_input_evidence,
    verify_capture_readiness,
    verify_input_readiness,
    verify_progress_streams,
    check_runtime_capacity,
    read_observation_stream,
    verify_observation_streams,
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


def test_main_suite_and_fail_closed() -> None:
    from tools.automation.eden_main_flow import MAIN_SCENARIOS, MainFlow, verify_main
    assert scenario_names("smoke") == SCENARIOS
    assert scenario_names("main") == SCENARIOS + MAIN_SCENARIOS
    assert len(set(scenario_names("main"))) == len(scenario_names("main"))
    rejected(lambda: scenario_names("unknown"))
    class Fake(MainFlow):
        def __init__(self):
            self.confirmations = 0
        def wait_ui(self, **expected):
            return {}
        def observe(self):
            return {"ui": {"overlay": 3}}
        def confirm(self):
            self.confirmations += 1
        def wait_receipt(self, kind, before):
            return {"status": "error"}
    flow = Fake()
    rejected(lambda: flow.submitted("set_weekly_template", set()))
    assert flow.confirmations == 3  # Bounded dialog handling, no business-action retry.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in MAIN_SCENARIOS:
            save(root / f"assertions/{name}.json", {"passed": True, "checks": ["fixture"]})
        # A collection of positive flags without underlying business material
        # cannot qualify as a completed journey.
        rejected(lambda: verify_main(root))
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        save(root / "evidence-manifest.json", {"schema_version": 1, "files": [{"path": "../outside"}]})
        rejected(lambda: verify(root))


def test_support_suite_evidence_gates() -> None:
    from copy import deepcopy
    from tools.automation.eden_support_flow import (
        SUPPORT_SCENARIOS, verify_diagnostic, verify_restored_status, verify_support,
    )
    assert scenario_names("support") == SUPPORT_SCENARIOS
    assert len(set(SUPPORT_SCENARIOS)) == 6 and "pin_wrong" not in SUPPORT_SCENARIOS
    diagnostic = {"version": 1, "redaction": "credentials-auth-codes-and-nonces-omitted",
                  "runtime_snapshot": {"status_loaded": True, "disable_flag_present": True, "status_age_seconds": 0}}
    verify_diagnostic(diagnostic, True)
    rejected(lambda: verify_diagnostic(diagnostic, False))
    diagnostic["config.json"] = {"nested": [{"grant_secret": "private"}]}
    rejected(lambda: verify_diagnostic(diagnostic, True))
    baseline = {"state": {"limited_today": 1, "blocked_today": 0, "unrestricted_today": 0,
                          "play_timer_enabled": 1, "remaining_minutes": 90, "played_minutes": 30}}
    restored = {"status": "ok", "setup": {"phase": "restored", "disable_flag_present": True, "recovery_active": False},
                "state": {**baseline["state"], "remaining_available": True, "played_minutes_available": True}}
    verify_restored_status(restored, baseline)
    changed = deepcopy(restored)
    changed["state"]["remaining_minutes"] = 120
    rejected(lambda: verify_restored_status(changed, baseline))
    changed = deepcopy(restored)
    changed["setup"]["recovery_active"] = True
    rejected(lambda: verify_restored_status(changed, baseline))
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in SUPPORT_SCENARIOS[1:]:
            save(root / f"assertions/{name}.json", {"passed": True, "checks": ["fixture"]})
        rejected(lambda: verify_support(root))  # Positive flags cannot replace underlying files/receipts.


def test_post_screenshot_neutral_progress() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        cached = {"instance": "a", "sequence": 10, "input_sequence": 20, "input_active": False, "ui": {"waiting": False}}
        first = {**cached, "sequence": 11, "input_sequence": 21}
        second = {**cached, "sequence": 12, "input_sequence": 22}
        with patch.object(suite, "observe", side_effect=[cached, cached, first, first, second]), \
                patch("tools.automation.eden_runner.time.sleep"):
            suite.wait_capture_resume("test")
        save(root / "checkpoints/test.json", {"screenshot_call": 1})
        verify_capture_readiness(root)
        # A screenshot followed only by cached/identical neutral data is not readiness.
        path = root / "capture-readiness/test-2.json"
        path.write_text(json.dumps(first), encoding="utf-8")
        rejected(lambda: verify_capture_readiness(root))
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        with patch.object(suite, "observe", side_effect=[cached, {**first, "instance": "b"}]), \
                patch("tools.automation.eden_runner.time.sleep"):
            rejected(lambda: suite.wait_capture_resume("wrong-instance"))


def test_observation_stream_partial_tail() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "observations.jsonl"
        path.write_bytes(b'{"sequence":1}\n{"sequence":2')
        assert read_observation_stream(path)[0] == {"sequence": 1}
        with path.open("ab") as stream:
            stream.write(b'}\n')
        assert read_observation_stream(path)[0] == {"sequence": 2}
        # Read a bounded tail even when the first byte of that tail is inside
        # an old record, and reject a corrupt completed last record.
        path.write_bytes(b'x' * 100000 + b'\n{"sequence":3}\n')
        assert read_observation_stream(path)[0] == {"sequence": 3}
        with path.open("ab") as stream:
            stream.write(b'not-json\n')
        rejected(lambda: read_observation_stream(path))
        path.write_bytes(b'{"sequence":4')
        rejected(lambda: read_observation_stream(path))
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        value = {"instance": "a", "sequence": 1, "ui": {"view": 0}}
        save(root / "input-samples/001-sample.json", value)
        path = root / "app-final/support/automation-ui.jsonl"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(value) + '\n{"unfinished":', encoding="utf-8")
        verify_observation_streams(root)
        path.write_text(json.dumps({**value, "ui": {"view": 1}}) + '\n', encoding="utf-8")
        rejected(lambda: verify_observation_streams(root))
        path.write_text(json.dumps({**value, "sequence": 2}) + '\n', encoding="utf-8")
        rejected(lambda: verify_observation_streams(root))
        path.write_text(json.dumps(value) + '\n', encoding="utf-8")
        save(root / "input-readiness/001-0.json", {**value, "sequence": 2})
        rejected(lambda: verify_observation_streams(root))  # Readiness also requires original raw records.


def test_preview_cancel_does_not_exit_child() -> None:
    from tools.automation.eden_main_flow import MainFlow
    class Fake(MainFlow):
        def action(self, **kwargs):
            self.buttons.append(kwargs["button"])
        def wait_ui(self, **expected):
            self.expected = expected
        def ids(self):
            return set()
        def type_code(self, code):
            return set()
        def wait_receipt(self, kind, before):
            return {"status": "ok"}
        def checked(self, *args, **kwargs):
            self.passed = True
    with tempfile.TemporaryDirectory() as directory:
        fake = Fake()
        fake.app = Path(directory)
        (fake.app / "results").mkdir()
        (fake.app / "ledger").mkdir()
        (fake.app / "rules.json").write_text('{}', encoding="utf-8")
        (fake.app / "ledger/used_nonces.jsonl").write_text('', encoding="utf-8")
        fake.code_day = 2488
        fake.buttons = []
        fake.code_cancel_journey()
        assert fake.passed and fake.buttons == ["B", "B"]
        assert fake.expected == {"view": 0, "overlay": 0}


def test_stale_observation_waits_without_input() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        fresh = {"ui": {"overlay": 0, "overlay_animating": False}}
        with patch.object(suite, "observe", side_effect=[ObservationPending("stale"), fresh]), \
                patch("tools.automation.eden_runner.time.sleep"):
            assert suite.wait_ui(overlay=0) == fresh
        with patch.object(suite, "observe", side_effect=GateError("wrong identity")):
            rejected(lambda: suite.wait_ui(overlay=0))


def test_sampled_input_releases_once_on_failure() -> None:
    class Client:
        def __init__(self):
            self.calls = []
        def tool(self, name, arguments=None):
            self.calls.append((name, arguments))
            return {"running": True, "backend": "eden-control", "pid": 1,
                    "launch": {"target_reached": True, "phase": "game_ready"}}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for fails in (False, True):
            client = Client()
            suite = Suite(client, root, root, {}, 60)
            neutral = {"instance": "a", "input_sequence": 1, "input_active": False}
            sampled = {"instance": "a", "input_sequence": 2, "input_active": True}
            sequence = [GateError("not sampled") if fails else sampled, neutral]
            with patch.object(suite, "wait_action_ready", return_value=neutral), \
                    patch.object(suite, "wait_input", side_effect=sequence), \
                    patch("tools.automation.eden_runner.time.sleep"):
                if fails:
                    rejected(lambda: suite.action(button="A"))
                else:
                    suite.action(button="A")
            assert [args["action"] for name, args in client.calls if name == "eden_press_button"] == ["down", "up"]
        client = Client()
        suite = Suite(client, root, root, {}, 60)
        with patch.object(suite, "wait_action_ready", side_effect=GateError("no presented progress")):
            rejected(lambda: suite.action(button="A"))
        assert not any(name == "eden_press_button" for name, _ in client.calls)
        suite = Suite(None, root, root, {}, 60)
        with patch.object(suite, "observe_input", return_value={"input_sampling_version": True, "input_sequence": 1, "input_active": False}):
            rejected(lambda: suite.wait_input(False))
        with patch.object(suite, "observe_input", return_value={"instance": "b", "input_sampling_version": 2, "input_sequence": 2, "input_active": True}):
            rejected(lambda: suite.wait_input(True, after=neutral))


def test_input_requires_presented_progress() -> None:
    from copy import deepcopy
    first = {"instance": "a", "presentation_version": 1, "input_sampling_version": 2,
             "sequence": 10, "input_sequence": 20, "input_active": False,
             "ui": {"waiting": False, "overlay_animating": False}}
    second = {**first, "sequence": 11, "input_sequence": 21}
    third = {**first, "sequence": 12, "input_sequence": 22}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        fourth = {**third, "sequence": 13, "input_sequence": 23}
        with patch.object(suite, "wait_input", return_value={"instance": "a", "input_sequence": 21}), \
                patch.object(suite, "observe", side_effect=[first, first, second, third, fourth]), \
                patch("tools.automation.eden_runner.time.sleep"):
            assert suite.wait_action_ready() == fourth
        assert json.loads((root / "input-readiness/001-0.json").read_text())["input_sequence"] == 21
        verify_input_readiness(root, 1)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        with patch.object(suite, "wait_input", return_value=first), \
                patch.object(suite, "observe", side_effect=[first, first, second, second, third]), \
                patch("tools.automation.eden_runner.time.sleep"):
            assert suite.wait_action_ready() == third
        verify_input_readiness(root, 1)
        verify_input_readiness(root, 1)
        path = root / "input-readiness/001-2.json"
        for bad in (first, {**third, "instance": "b"}, {**third, "presentation_version": True},
                    {**third, "ui": {"waiting": True, "overlay_animating": False}}):
            path.write_text(json.dumps(bad), encoding="utf-8")
            rejected(lambda: verify_input_readiness(root, 1))
        path.unlink()
        rejected(lambda: verify_input_readiness(root, 1))
    for bad in ({**first, "presentation_version": 0}, {**first, "instance": "b"}):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            suite = Suite(None, root, root, {}, 60)
            with patch.object(suite, "wait_input", return_value=first), \
                    patch.object(suite, "observe", return_value=bad):
                rejected(lambda: suite.wait_action_ready())
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        pending = {**third, "ui": {"waiting": True, "overlay_animating": False}}
        after = [{**first, "sequence": i, "input_sequence": i + 10} for i in (13, 14, 15)]
        with patch.object(suite, "wait_input", return_value=first), \
                patch.object(suite, "observe", side_effect=[first, second, pending, *after]), \
                patch("tools.automation.eden_runner.time.sleep"):
            assert suite.wait_action_ready() == after[-1]
        assert json.loads((root / "input-readiness/001-0.json").read_text())["sequence"] == 13
        verify_input_readiness(root, 1)
    # Cached snapshots and a still-rendering dialog never become readiness,
    # even when the model says input is currently neutral.
    for value in (first, {**third, "ui": {"waiting": False, "overlay_animating": True}}):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            suite = Suite(None, root, root, {}, 60)
            ticks = iter(index / 4 for index in range(100))
            with patch.object(suite, "wait_input", return_value=first), \
                    patch.object(suite, "observe", return_value=deepcopy(value)), \
                    patch("tools.automation.eden_runner.time.monotonic", side_effect=lambda: next(ticks)), \
                    patch("tools.automation.eden_runner.time.sleep"):
                rejected(lambda: suite.wait_action_ready())
            assert not (root / "input-readiness").exists()


def test_failure_metrics_preserve_original_error() -> None:
    class Client:
        def __init__(self, fail: bool):
            self.fail, self.calls = fail, []
        def tool(self, name, arguments=None, **kwargs):
            self.calls.append((name, arguments))
            if self.fail:
                raise RuntimeError("diagnostic unavailable")
            return {"frame_counter_before": 42, "frame_counter_after": 42}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for fail in (False, True):
            client = Client(fail)
            suite = Suite(client, root, root, {}, 60)
            name = f"input-{fail}"
            def stopped():
                raise GateError("original sampling stop")
            with patch.object(suite, "capture", side_effect=GateError("capture unavailable")):
                try:
                    suite.run_case(name, stopped)
                except GateError as exc:
                    assert str(exc) == "original sampling stop"
                else:
                    raise AssertionError("original failure was swallowed")
            assert client.calls == [("eden_get_runtime_metrics", {"sample_ms": 500})]
            assert suite.results[name]["status"] == "failed_or_unknown"
            assert (root / f"failure-metrics/{name}{'-error' if fail else ''}.json").is_file()


def test_progress_stream_boundaries() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        path = root / "app-final/support/automation-progress.jsonl"
        path.parent.mkdir(parents=True)
        first = {"schema_version": 1, "instance": "a", "sequence": 1, "tick": "1", "stage": 7,
                 "detail": 0, "input_sequence": 5, "input_active": False}
        second = {**first, "sequence": 2, "stage": 16}
        path.write_text(json.dumps(first) + '\n' + json.dumps(second) + '\n{"partial":', encoding="utf-8")
        verify_progress_streams(root)
        for bad in ({**second, "stage": True}, {**second, "sequence": 1}, {**second, "instance": "b"},
                    {**second, "stage": 17}, {**second, "input_sequence": 4}):
            path.write_text(json.dumps(first) + '\n' + json.dumps(bad) + '\n', encoding="utf-8")
            rejected(lambda: verify_progress_streams(root))


def test_profile_capacity_before_copy() -> None:
    from types import SimpleNamespace
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory) / "runtime"
        required = 1000 + 512 * 1024 * 1024
        with patch("tools.automation.eden_runner.shutil.disk_usage", return_value=SimpleNamespace(free=required)):
            result = check_runtime_capacity(root, 1000)
        assert result["free_before"] == required and result["copy_bytes"] == 1000
        with patch("tools.automation.eden_runner.shutil.disk_usage", return_value=SimpleNamespace(free=required - 1)):
            rejected(lambda: check_runtime_capacity(root, 1000))
        assert list(root.iterdir()) == []
        rejected(lambda: check_runtime_capacity(root, True))
    import argparse
    from contextlib import redirect_stdout
    from io import StringIO
    from tools.automation.eden_runner import run
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory) / "refused"
        args = argparse.Namespace(out=root, suite="support")
        # Isolate only this module's platform boundary; leave pathlib and the
        # real OS untouched so the preparation rejection runs on Host Linux too.
        with patch("tools.automation.eden_runner.os", SimpleNamespace(name="nt")), \
                patch("tools.automation.eden_runner.prepare", side_effect=GateError("runtime disk space insufficient")), \
                patch("tools.automation.eden_runner.McpClient") as client, redirect_stdout(StringIO()):
            assert run(args) == 1
        client.assert_not_called()
        refusal = json.loads((root / "preparation.json").read_text())
        assert refusal["phase"] == "preparation" and refusal["status"] == "precondition_not_met"
        assert all(case["status"] == "not_executed" for case in refusal["scenarios"].values())


def test_input_evidence_roundtrip() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for number, action in enumerate(("down", "up")):
            save(root / f"calls/{number:04d}.json", {"params": {"name": "eden_press_button", "arguments": {"button": "A", "action": action}}})
        save(root / "input-samples/001-active.json", {"instance": "a", "input_sequence": 2, "input_active": True})
        save(root / "input-samples/001-release.json", {"instance": "a", "input_sequence": 3, "input_active": False})
        verify_input_evidence(root, 1)
        verify_input_evidence(root, 1)
        (root / "input-samples/001-release.json").write_text('{"instance":"b","input_sequence":3,"input_active":false}', encoding="utf-8")
        rejected(lambda: verify_input_evidence(root, 1))
        rejected(lambda: verify_input_evidence(root, True))


def test_independent_input_while_presentation_blocked() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        active = {"schema_version": 1, "input_stream_version": 1, "instance": "a", "tick": "123",
                  "input_sampling_version": 2, "sequence": 2, "input_sequence": 2, "input_active": True}
        idle = {**active, "sequence": 3, "input_sequence": 3, "input_active": False}
        path = suite.app / "support/automation-input.jsonl"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(active) + '\n', encoding="utf-8")
        # The HID edge must be observable before a synchronous request, draw,
        # or presentation returns. The next down still uses the UI gate above.
        with patch.object(suite, "observe", side_effect=GateError("presentation blocked")):
            assert suite.wait_input(True, after={**idle, "input_sequence": 1}) == active
            path.write_text(json.dumps(idle) + '\n', encoding="utf-8")
            assert suite.wait_input(False, after=active) == idle
        for bad in ({**idle, "schema_version": True}, {**idle, "tick": 123},
                    {**idle, "input_sampling_version": 1}, {**idle, "sequence": True},
                    {**idle, "input_sequence": 2}, {**idle, "input_active": 0}):
            path.write_text(json.dumps(bad) + '\n', encoding="utf-8")
            rejected(suite.observe_input)
        path.write_text(json.dumps(idle) + '\n', encoding="utf-8")
        with patch("tools.automation.eden_runner.time.time", return_value=path.stat().st_mtime + 5):
            try:
                suite.observe_input()
                raise AssertionError("stale HID sample accepted")
            except ObservationPending:
                pass
        # UI and HID sequences have independent namespaces, even in one instance.
        save(root / "input-samples/001-active.json", active)
        save(root / "input-samples/001-release.json", idle)
        save(root / "input-readiness/001-0.json", {"instance": "a", "sequence": 2, "ui": {"view": 0}})
        raw = root / "app-final/support"
        raw.mkdir(parents=True)
        (raw / "automation-input.jsonl").write_text(json.dumps(active) + '\n' + json.dumps(idle) + '\n', encoding="utf-8")
        (raw / "automation-ui.jsonl").write_text(json.dumps({"instance": "a", "sequence": 2, "ui": {"view": 0}}) + '\n', encoding="utf-8")
        verify_observation_streams(root)
        verify_observation_streams(root)
        (raw / "automation-input.jsonl").write_text(json.dumps(idle) + '\n', encoding="utf-8")
        rejected(lambda: verify_observation_streams(root))
        with patch.object(suite, "observe_input", side_effect=[ObservationPending("stale"), idle]), \
                patch("tools.automation.eden_runner.time.sleep"):
            assert suite.wait_input(False) == idle


def test_action_across_independent_stream_timings() -> None:
    class Client:
        def __init__(self):
            self.calls = []
        def tool(self, name, arguments=None):
            self.calls.append((name, arguments))
            return {"running": True, "backend": "eden-control", "pid": 1,
                    "launch": {"target_reached": True, "phase": "game_ready"}}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        client = Client()
        suite = Suite(client, root, root, {}, 60)
        neutral = {"instance": "a", "input_stream_version": 1, "input_sampling_version": 2,
                   "input_sequence": 19, "input_active": False}
        frames = [{"instance": "a", "presentation_version": 1, "input_sampling_version": 2,
                   "sequence": seq, "input_sequence": seq, "input_active": False,
                   "ui": {"waiting": False, "overlay_animating": False}} for seq in (18, 20, 21, 22)]
        active = {**neutral, "input_sequence": 24, "input_active": True}
        release = {**neutral, "input_sequence": 25}
        with patch.object(suite, "observe", side_effect=frames), \
                patch.object(suite, "observe_input", side_effect=[neutral, neutral, active, active, release]), \
                patch("tools.automation.eden_runner.time.sleep"):
            suite.action(button="A")
        assert [args["action"] for name, args in client.calls if name == "eden_press_button"] == ["down", "up"]
        verify_input_readiness(root, 1)
        # Older UI/HID publications can cross in either direction, but the
        # independent HID stream itself must never regress or change instance.
        for bad in ({**active, "input_sequence": 23}, {**release, "instance": "b"}):
            with patch.object(suite, "observe_input", return_value=bad):
                rejected(lambda: suite.wait_input(False, after=active))


def test_readonly_status_overlap_preserves_business_gate() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        suite = Suite(None, root, root, {}, 60)
        for name, completed in (("first", 10), ("second", 11)):
            save(suite.app / f"results/{name}.json", {"request_id": name, "type": "status",
                                                   "status": "ok", "completed_at": completed})
        assert suite.wait_receipt("status", set())["request_id"] == "second"
        assert len(list((root / "receipts").glob("*.json"))) == 2
        (suite.app / "results/first.json").write_text(json.dumps({"request_id": "first", "type": "status", "status": "error", "completed_at": 10}), encoding="utf-8")
        rejected(lambda: suite.wait_receipt("status", set()))
        for name in ("first", "second"):
            (suite.app / f"results/{name}.json").write_text(json.dumps({"request_id": name, "type": "set_eye_care_policy", "status": "ok"}), encoding="utf-8")
        rejected(lambda: suite.wait_receipt("set_eye_care_policy", set()))


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
    test_main_suite_and_fail_closed()
    test_support_suite_evidence_gates()
    test_post_screenshot_neutral_progress()
    test_observation_stream_partial_tail()
    test_preview_cancel_does_not_exit_child()
    test_stale_observation_waits_without_input()
    test_sampled_input_releases_once_on_failure()
    test_input_requires_presented_progress()
    test_failure_metrics_preserve_original_error()
    test_progress_stream_boundaries()
    test_profile_capacity_before_copy()
    test_input_evidence_roundtrip()
    test_independent_input_while_presentation_blocked()
    test_action_across_independent_stream_timings()
    test_readonly_status_overlap_preserves_business_gate()
    print("PASS: Eden input/exit gates, stdio failures, identity and evidence roundtrip")


if __name__ == "__main__":
    main()
