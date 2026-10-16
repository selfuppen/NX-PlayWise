#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import hashlib
import struct
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import tools.legacy.usage_probe as probe
import tools.package_remote as package_remote
import tools.legacy.package_usage_probe as pkg
import tools.legacy.usage_probe_autostart as autostart
from tools.legacy.usage_probe_analysis import focus_for_samples, focus_window, stats_window, unique_events


def sample(remaining: int, spent: int, minutes: int = 60) -> dict:
    return {"sample_error": 0, "remaining_result": 0, "spent_result": 0,
            "remaining_ns": str(remaining * probe.NS), "spent_ns": str(spent * probe.NS),
            "configured_available": True, "configured_minutes": minutes,
            "boot_id": "boot1", "tick_ns": "1000000000"}


class UsageProbeTests(unittest.TestCase):
    def test_signed_overrun_and_two_grants(self) -> None:
        for minutes in (60, 180, 300):
            s = sample((minutes - 250) * 60, 250 * 60, minutes)
            self.assertEqual(probe.projection(s, clamp=False), 250 * 60 * probe.NS)
            self.assertEqual(probe.projection(s, clamp=True), min(minutes, 250) * 60 * probe.NS)

    def test_failed_ipc_never_zero(self) -> None:
        s = sample(0, 0)
        s["sample_error"] = 400
        self.assertIsNone(probe.counter(s, "remaining_ns"))
        self.assertIsNone(probe.projection(s, clamp=False))
        s["sample_error"] = 0
        s["spent_result"] = 123
        self.assertIsNone(probe.counter(s, "spent_ns"))

    def test_exact_nanoseconds(self) -> None:
        s = sample(0, 0)
        s["spent_ns"] = "9223372036854775806"
        self.assertEqual(probe.counter(s, "spent_ns"), 9223372036854775806)

    def test_missing_or_unlimited_setting_unknown(self) -> None:
        s = sample(1, 2)
        s["configured_available"] = False
        self.assertIsNone(probe.projection(s, clamp=False))
        s.update(configured_available=True, configured_minutes=65535)
        self.assertIsNone(probe.projection(s, clamp=False))

    def test_reboot_does_not_subtract_ticks(self) -> None:
        a, b = sample(60, 30), sample(59, 31)
        b.update(boot_id="boot2", tick_ns="0")
        result = probe.compare(a, b)
        self.assertIsNone(result["elapsed_s"])
        self.assertEqual(result["spent_ns_delta_s"], 1)

    def test_clock_jump_uses_monotonic(self) -> None:
        a, b = sample(60, 30), sample(59, 31)
        a["unix_s"], b["unix_s"] = "100", "99999"
        b["tick_ns"] = "2000000000"
        self.assertEqual(probe.compare(a, b)["elapsed_s"], 1)

    def test_recovery_during_window_is_not_stable_allowance(self) -> None:
        a, b = sample(60, 30, 125), sample(60, 30, 180)
        a["settings_hex"], b["settings_hex"] = "125", "180"
        self.assertFalse(probe.compare(a, b)["settings_stable"])
        self.assertEqual(probe.compare(a, b)["after_configured_minutes"], 180)

    def test_truncated_jsonl_preserves_valid_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "samples.jsonl"
            p.write_text('{"sample_error":0}\n{"unfinished":', encoding="utf-8")
            values, damaged = probe.rows(p)
            self.assertTrue(damaged)
            self.assertEqual(len(values), 1)

    def test_report_keeps_observation_pending(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            probe.dump(p / "run.json", {"actions": [], "phases": [{"name": "sleep", "status": "precondition_not_met", "reason": "no automatic wake"}]})
            summary = probe.report(p)
            self.assertTrue(summary["manual_review_pending"])
            self.assertEqual(summary["captured_phases"], 0)
            self.assertIn("no automatic wake", (p / "index.html").read_text(encoding="utf-8"))

    def test_build_flag_explicit_and_same_session(self) -> None:
        default = package_remote.container_command("/ws/playwise")
        selected = package_remote.container_command("/ws/playwise", with_usage_probe=True)
        self.assertNotIn("usage-probe-package", default)
        self.assertIn("usage-probe-package", selected)
        self.assertIn("package-playwise", selected)
        self.assertIn("eden-test-nro", selected)
        self.assertIn("make", selected)

    def test_focus_union_does_not_double_count_overlap(self) -> None:
        events = [
            {"index": 0, "raw_hex": "a", "type": 0, "event_type": 2, "program_id": "game", "event_steady_s": "10"},
            {"index": 1, "raw_hex": "b", "type": 0, "event_type": 2, "program_id": "home", "event_steady_s": "20"},
            {"index": 2, "raw_hex": "c", "type": 0, "event_type": 4, "program_id": "game", "event_steady_s": "30"},
            {"index": 3, "raw_hex": "d", "type": 0, "event_type": 4, "program_id": "home", "event_steady_s": "40"},
        ]
        result = focus_window(events + events, 10, 40)
        self.assertEqual(result["candidate_focus_union_s"], 30)
        self.assertEqual(result["by_program_s"], {"game": 20, "home": 20})
        self.assertFalse(result["semantics_confirmed"])
        self.assertEqual(len(unique_events(events + events)), 4)

    def test_unclosed_focus_tail_remains_explicit(self) -> None:
        result = focus_window([{"index": 0, "raw_hex": "a", "type": 0, "event_type": 2, "program_id": "home", "event_steady_s": "10"}], 20, 30)
        self.assertEqual(result["open_tail_s"], 10)
        self.assertFalse(result["semantics_confirmed"])

    def test_stats_does_not_sum_accounts(self) -> None:
        base = {"result": 0, "program_id": "game", "account_ordinal": 0}
        values = [dict(base, steady_s="10", playtime_ns="100"), dict(base, steady_s="20", playtime_ns="200"),
                  dict(base, account_ordinal=1, steady_s="20", playtime_ns="999")]
        self.assertEqual(stats_window(values, 10, 20)["programs"]["game"]["delta_ns"], "100")

    def test_probe_package_rejects_tampering_and_boot_flag(self) -> None:
        source_hash = "a" * 64
        nro = bytearray(0x80)
        nro[0x10:0x14] = b"NRO0"
        nso = bytearray(0x100)
        nso[:4] = b"NSO0"
        struct.pack_into("<III", nso, 0x10, 0x100, 0, 64)
        struct.pack_into("<III", nso, 0x20, 0x140, 0, 0)
        struct.pack_into("<III", nso, 0x30, 0x140, 0, 0)
        nso.extend(source_hash.encode())
        nsp = b"PFS0" + struct.pack("<III", 1, 5, 0) + struct.pack("<QQII", 0, len(nso), 0, 0) + b"main\0" + nso
        artifacts = {f"atmosphere/contents/{pkg.TITLE_ID}/exefs.nsp": nsp,
                     "switch/playwise-usage-probe/launcher.nro": bytes(nro) + source_hash.encode()}
        identity = {"profile": "usage-probe", "advisory": True, "title_id": pkg.TITLE_ID, "source_hash": source_hash,
                    "artifacts": {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in artifacts.items()}}
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "probe.zip"
            def write(extra: bool = False, corrupt: bool = False) -> None:
                with zipfile.ZipFile(path, "w") as z:
                    for name, data in artifacts.items():
                        z.writestr(name, data + (b"corrupt" if corrupt else b""))
                    z.writestr("switch/playwise-usage-probe/build.json", json.dumps(identity))
                    if extra:
                        z.writestr(f"atmosphere/contents/{pkg.TITLE_ID}/flags/boot2.flag", b"")
            write()
            self.assertEqual(pkg.verify(path)["source_hash"], source_hash)
            write(corrupt=True)
            with self.assertRaises(ValueError):
                pkg.verify(path)
            write(extra=True)
            with self.assertRaises(ValueError):
                pkg.verify(path)

    def test_lz4_compressed_identity_and_overlapping_match(self) -> None:
        identity = b"a" * 64
        self.assertEqual(pkg.lz4_block(b"\xf0\x31" + identity, 64), identity)
        self.assertEqual(pkg.lz4_block(b"\x35abc\x03\x00", 12), b"abcabcabcabc")
        with self.assertRaises(ValueError):
            pkg.lz4_block(b"\x00\x00\x00", 4)

    def test_screenshot_failure_keeps_machine_capture(self) -> None:
        class FakeDevice:
            n = 0
            def command(self, op: str) -> dict:
                self.n += 1
                s = sample(50-self.n, 20+self.n)
                s["tick_ns"] = str(self.n * probe.NS)
                return {"ok": True, "after": s}
            def screenshot(self, path: Path) -> None:
                raise OSError("capture unavailable")
        class FakeClock:
            t = 0
            def __call__(self) -> int:
                self.t += 2
                return self.t
        with tempfile.TemporaryDirectory() as d, mock.patch.object(probe.time, "monotonic", FakeClock()):
            e = probe.Experiment(FakeDevice(), Path(d))
            e.phase("home", 1, requested_context="HOME")
            self.assertEqual(e.phases[0]["status"], "captured")
            self.assertEqual(len(e.phases[0]["screenshot_errors"]), 2)

    def test_late_annotation_cannot_reverse_time(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            a, b, late = sample(50, 20), sample(49, 21), sample(48, 22)
            b["tick_ns"], late["tick_ns"] = "2000000000", "3000000000"
            probe.dump(p / "run.json", {"actions": [], "phases": [{"name": "settings", "status": "captured", "before": a, "after": b}]})
            probe.dump(p / "phase-annotations.json", {"settings": {"corrected_before": late}})
            summary = probe.report(p)
            self.assertEqual(summary["phases"][0]["comparison"]["elapsed_s"], 1)
            self.assertEqual(summary["phases"][0]["scenario_status"], "precondition_not_met")

    def test_live_export_resumes_after_download_failure(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            e = probe.Experiment(None, Path(d))
            with mock.patch.object(e, "action") as action, mock.patch.object(e, "_download", side_effect=OSError("FTP disconnected")):
                with self.assertRaises(OSError):
                    e.download(live=True)
                self.assertEqual(action.call_args_list, [mock.call("export"), mock.call("collect")])

    def test_human_observation_remains_picture_classification(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            probe.dump(p / "run.json", {"actions": [], "phases": [{"name": "overrun-limit-1", "status": "captured", "screenshots": ["screen.jpg"]}]})
            probe.dump(p / "manual-observations.json", {"observations": {"overrun-limit-1": "限制提示"}})
            summary = probe.report(p)
            self.assertFalse(summary["manual_review_pending"])
            self.assertEqual(summary["phases"][0]["manual_observation"], "限制提示")
            self.assertNotIn("software_paused", summary["phases"][0])
            self.assertIn("限制提示", (p / "index.html").read_text(encoding="utf-8"))

    def test_held_override_key_is_released_on_input_failure(self) -> None:
        bot = mock.MagicMock()
        bot.__enter__.return_value = bot
        bot.click.side_effect = OSError("disconnected")
        device = probe.Device("test")
        with mock.patch.object(device, "bot", return_value=bot):
            with self.assertRaises(OSError):
                device.buttons("+X,A,-X")
        bot.press.assert_called_once_with("X")
        bot.release.assert_called_once_with("X")

    def test_pdm_distinct_steady_epoch_uses_bounded_user_window(self) -> None:
        before = {"unix_s": "1010", "steady_s": "510", "steady_source": "same"}
        after = {"unix_s": "1030", "steady_s": "530", "steady_source": "same"}
        events = [
            {"index": 0, "raw_hex": "a", "type": 0, "event_type": 2, "program_id": "game", "event_user_s": "1010", "event_steady_s": "10", "unix_s": "1011", "steady_s": "511"},
            {"index": 1, "raw_hex": "b", "type": 0, "event_type": 4, "program_id": "game", "event_user_s": "1020", "event_steady_s": "20", "unix_s": "1021", "steady_s": "521"},
        ]
        result = focus_for_samples(events, before, after)
        self.assertEqual(result["candidate_focus_union_s"], 10)
        self.assertEqual(result["observed_event_to_sample_steady_offsets_s"], [500])
        self.assertEqual(result["open_tail_s"], 0)
        self.assertFalse(result["semantics_confirmed"])

    def test_pdm_clock_jump_is_not_an_aligned_window(self) -> None:
        a = {"unix_s": "100", "steady_s": "10", "steady_source": "same"}
        b = {"unix_s": "10000", "steady_s": "20", "steady_source": "same"}
        self.assertEqual(focus_for_samples([], a, b)["reason"], "clock_discontinuity")

    def test_pdm_application_tag_detects_stale_open_game(self) -> None:
        a = {"unix_s": "100", "steady_s": "10", "steady_source": "same", "application_id": "current"}
        b = dict(a, unix_s="110", steady_s="20")
        events = [{"index": 0, "raw_hex": "a", "type": 0, "event_type": 2, "applet_id": 1,
                   "program_id": "old", "event_user_s": "90"},
                  {"index": 1, "raw_hex": "b", "type": 0, "event_type": 2, "applet_id": 3,
                   "program_id": "home", "event_user_s": "100"}]
        result = focus_for_samples(events, a, b)
        self.assertEqual(result["unclosed_other_applications"], ["old"])
        self.assertFalse(result["semantics_confirmed"])

    def test_autostart_requires_bound_readonly_identity(self) -> None:
        plan = {"plan_id": "p", "source_hash": "hash"}
        ready = {"source_hash": "hash", "boot_plan_id": "p", "boot_plan_valid": True, "read_only_boot": True, "boot_id": "new"}
        latest = {"boot_id": "new", "read_only_boot": True, "writing_enabled": False, "exclusive_owner": True}
        autostart.require_ready(ready, latest, plan)
        for field, value in (("writing_enabled", True), ("exclusive_owner", False), ("boot_id", "old"), ("read_only_boot", False)):
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                autostart.require_ready(ready, dict(latest, **{field: value}), plan)
        with self.assertRaises(RuntimeError):
            autostart.require_ready(dict(ready, source_hash="other"), latest, plan)

    def test_journal_retries_transient_windows_replace_denial(self) -> None:
        original = Path.replace
        calls = []
        def replace(path, target):
            calls.append(path)
            if len(calls) < 3:
                raise PermissionError("reader holds file")
            return original(path, target)
        with tempfile.TemporaryDirectory() as root, mock.patch.object(Path, "replace", replace), mock.patch.object(probe.time, "sleep"):
            target = Path(root) / "journal.json"
            target.write_text('{"old":true}', encoding="utf-8")
            probe.dump(target, {"new": True})
            self.assertEqual(json.loads(target.read_text(encoding="utf-8")), {"new": True})
            self.assertEqual(len(calls), 3)

    def test_autostart_optional_read_does_not_hide_permission_errors(self) -> None:
        import ftplib
        d = mock.Mock()
        d.read.side_effect = ftplib.error_perm("550 missing")
        self.assertIsNone(autostart.optional(d, "/missing"))
        d.read.side_effect = ftplib.error_perm("530 not logged in")
        with self.assertRaises(ftplib.error_perm):
            autostart.optional(d, "/private")

    def test_autostart_waits_for_first_new_sample(self) -> None:
        plan = {"plan_id": "p", "source_hash": "hash", "before_probe_session": "old"}
        ready = {"source_hash": "hash", "boot_plan_id": "p", "boot_plan_valid": True,
                 "read_only_boot": True, "boot_id": "new"}
        latest = {"boot_id": "new", "read_only_boot": True, "writing_enabled": False,
                  "exclusive_owner": True, "tick_ns": "1"}
        d = mock.Mock()
        d.json.side_effect = [ready, {"boot_id": "old"}, ready, latest, dict(latest, tick_ns="2")]
        with tempfile.TemporaryDirectory() as root, mock.patch.object(autostart.time, "sleep"):
            observed = autostart.wait_ready(d, Path(root), plan)
            self.assertEqual(observed["boot_id"], "new")
            self.assertTrue((Path(root) / "startup-verification.json").exists())

    def test_autostart_export_rejects_growing_frozen_log(self) -> None:
        e = mock.MagicMock()
        ftp = e.d.ftp.return_value.__enter__.return_value
        ftp.size.side_effect = [100, 110]
        with self.assertRaisesRegex(RuntimeError, "export barrier"):
            autostart.export_roundtrip(e)
        self.assertEqual(e.action.call_args_list, [mock.call("export"), mock.call("collect")])

    def test_autostart_export_proves_freeze_and_resume(self) -> None:
        e = mock.MagicMock()
        ftp = e.d.ftp.return_value.__enter__.return_value
        ftp.size.side_effect = [100, 100, 120]
        e.action.side_effect = [{"after": sample(50, 20)},
                                {"before": sample(49, 21), "after": {"writing_enabled": False}}]
        with mock.patch.object(autostart, "phase"):
            result = autostart.export_roundtrip(e)
        self.assertEqual(result["frozen_size_before"], result["frozen_size_after"])
        self.assertGreater(result["resumed_size"], result["frozen_size_after"])
        self.assertFalse(result["writing_enabled_after_collect"])


if __name__ == "__main__":
    unittest.main()
