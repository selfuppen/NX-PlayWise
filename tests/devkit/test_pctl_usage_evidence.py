#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import pctl_usage_evidence as evidence
import package_pctl_capture as capture
import package_remote
import pctl_capture as live
import pctl_usage_scenarios as scenarios
import pctl_mainline_report as mainline_report
import pctl_midnight_evidence as midnight
import pctl_next_day_usage as next_day
import pctl_fresh_takeover as fresh


def sample(quota: int, spent: int, tick: int = 1) -> dict:
    return {"sample_error": 0, "spent_result": 0, "spent_ns": str(spent * 10**9),
            "remaining_result": 0, "remaining_ns": str((quota * 60-spent) * 10**9),
            "configured_available": True, "configured_minutes": quota, "boot_id": "same", "tick_ns": str(tick * 10**9)}


class EvidenceTests(unittest.TestCase):
    def test_post_grant_window_requires_contiguous_usable_sixty_seconds(self) -> None:
        rows = [dict(sample(180, 4000+t, t), day_index=2478, timer_enabled=True, restricted=False, timer_result=0, restricted_result=0, settings_result=0) for t in range(5, 71, 5)]
        self.assertEqual(fresh.post_grant_window(rows, rows[0])["comparison"]["spent_ns_delta_s"], 65)
        for bad in (rows[:12], rows[:3]+rows[6:], [dict(s, restricted=i==3) for i,s in enumerate(rows)], [dict(s, spent_result=1) for s in rows], [dict(s, configured_minutes=60 if i==3 else 180) for i,s in enumerate(rows)]):
            with self.assertRaises(RuntimeError):
                fresh.post_grant_window(bad, rows[0])

    def test_host_phase_waits_for_device_tick_duration(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            (out/"capture-install.json").write_text("{}", encoding="utf-8")
            task = scenarios.Mainline(mock.Mock(), out)
            a, b = sample(180, 4000, 5), sample(180, 4065, 70)
            task.sample = mock.Mock(side_effect=[a, b])
            with mock.patch.object(scenarios.time, "monotonic", side_effect=[0, 61]):
                task.phase("bounded", 60)
            task.sample.assert_has_calls([mock.call(), mock.call(after_tick=int(a["tick_ns"])+60*10**9-1)])

    def test_fresh_test_pin_matches_companion_and_has_no_setup_fields(self) -> None:
        import hashlib
        import hmac
        salt = bytes(range(16))
        with mock.patch.object(fresh.secrets, "randbelow", return_value=123), mock.patch.object(fresh.secrets, "token_bytes", return_value=salt):
            auth = fresh.new_test_auth()
        self.assertEqual(auth["pin_hash"], hmac.new(b"000000000123", b"PTC-PIN1"+salt, hashlib.sha256).hexdigest())
        self.assertEqual(auth["pin_salt"], salt.hex())
        self.assertEqual(auth["failed_attempts"], 0)
        self.assertNotIn("phase", auth)
        self.assertNotIn("grant_secret", auth)

    def test_fresh_directory_targets_are_exact_and_cannot_escape(self) -> None:
        rid = "a"*32
        p = {"run_id": rid, "original_root": "/switch/playwise", "old_root_archive": "/switch/playwise-before-first-"+rid, "test_root_archive": "/switch/playwise-first-test-"+rid}
        self.assertEqual(fresh.roots(p)[0], "/switch/playwise")
        for patch in ({"run_id": "../bad"}, {"old_root_archive": "/switch"}, {"original_root": "/"}):
            with self.assertRaises(RuntimeError):
                fresh.roots(dict(p, **patch))

    def test_fresh_first_requires_actual_large_history_and_new_setup(self) -> None:
        raw = dict(sample(0, 3661), stage="before_standard_bootstrap", day_index=2478, settings_mode="unconfigured", settings_result=0, timer_result=0, restricted_result=0)
        setup = {"phase": "unconfigured", "snapshot_available": False}
        state = {"restriction_enabled": True, "temporary_unlocked": False, "restricted_now": 0}
        fresh.require_before_enable(raw, setup, state, 2478)
        for patch in ({"spent_ns": str(3600*10**9)}, {"settings_mode": "limited"}, {"spent_result": 1}, {"day_index": 2477}, {"stage": "standard_running"}):
            with self.assertRaises(RuntimeError):
                fresh.require_before_enable(dict(raw, **patch), setup, state, 2478)
        with self.assertRaises(RuntimeError):
            fresh.require_before_enable(raw, {"phase": "restored"}, state, 2478)

    def test_next_day_native_rule_and_counter_relation_are_required(self) -> None:
        s = dict(sample(60, 120), day_index=2478, settings_mode="limited", settings_result=0, timer_result=0, restricted_result=0, timer_enabled=True, restricted=False)
        next_day.require_usable(s, 2478, 60)
        for patch in ({"configured_minutes": 0}, {"day_index": 2477}, {"remaining_result": 1}, {"restricted": True}, {"remaining_ns": "0"}, {"timer_enabled": False}):
            with self.assertRaises(RuntimeError):
                next_day.require_usable(dict(s, **patch), 2478, 60)

    def test_natural_midnight_reports_nonreset_without_inventing_growth(self) -> None:
        a = dict(sample(0, 30000, 100), unix_s="998", day_index=2477, restricted=True, timer_enabled=False, timer_result=0, restricted_result=0, settings_result=0)
        b = dict(sample(0, 30000, 105), unix_s="1003", day_index=2478, restricted=True, timer_enabled=False, timer_result=0, restricted_result=0, settings_result=0)
        r = midnight.analyze([a, b], 1000)
        self.assertEqual(r["status"], "bounded_natural_date_transition_observed")
        self.assertFalse(r["boundaries"][0]["counter_reset_to_zero_at_boundary"])
        self.assertEqual(r["next_day_growth"], "not_tested_while_bedtime_restricted")
        self.assertEqual(r["signed_relation_mismatches"], [])

    def test_new_boot_or_gap_cannot_prove_continuous_midnight(self) -> None:
        a = dict(sample(0, 30000, 100), unix_s="998", day_index=2477)
        b = dict(sample(0, 0, 105), unix_s="1003", day_index=2478)
        for patch in ({"boot_id": "new"}, {"unix_s": "1200"}):
            r = midnight.analyze([a, dict(b, **patch)], 1000)
            self.assertEqual(r["status"], "incomplete_no_bounded_same_boot_date_transition")

    def test_sleep_gap_is_only_a_candidate_and_restriction_is_excluded(self) -> None:
        a = dict(sample(600, 100, 100), unix_s="1000", day_index=2477, source_hash="fixed", stage="standard_running", restricted=False)
        b = dict(sample(600, 104, 224), unix_s="1124", day_index=2477, source_hash="fixed", stage="standard_running", restricted=False)
        result = mainline_report.lifecycle_candidates([a, b])
        self.assertTrue(result["sleep_gaps"][0]["counter_frozen_candidate"])
        self.assertTrue(result["sleep_gaps"][0]["manual_sleep_confirmation_required"])
        self.assertNotEqual(result["status"], "passed")
        b["restricted"] = True
        self.assertFalse(mainline_report.lifecycle_candidates([a, b])["sleep_gaps"][0]["counter_frozen_candidate"])

    def test_reboot_candidate_rejects_reset_day_change_and_expired_endpoint(self) -> None:
        a = dict(sample(600, 100, 1000), unix_s="1000", day_index=2477, source_hash="fixed", stage="standard_running")
        b = dict(sample(600, 102, 30), boot_id="new", unix_s="1040", day_index=2477, source_hash="fixed", stage="before_standard_bootstrap")
        self.assertTrue(mainline_report.lifecycle_candidates([a, b])["reboot_pairs"][0]["bounded_retention_candidate"])
        for patch in ({"spent_ns": "0"}, {"day_index": 2478}, {"unix_s": "2000"}, {"source_hash": "different"}):
            result = mainline_report.lifecycle_candidates([a, dict(b, **patch)])
            self.assertFalse(result["reboot_pairs"][0]["bounded_retention_candidate"])

    def test_grant_changes_remaining_without_changing_used(self) -> None:
        r = evidence.transition(sample(125, 23176), sample(245, 23176, 2))
        self.assertEqual(r["direct_delta_ns"], "0")
        self.assertTrue(r["allowance_relation_after"])
        self.assertFalse(r["excessive_jump"])

    def test_wrong_display_like_quota_jump_is_rejected(self) -> None:
        r = evidence.transition(sample(125, 100), sample(245, 7300, 2))
        self.assertTrue(r["excessive_jump"])

    def test_failed_or_negative_direct_counter_is_unknown(self) -> None:
        for patch in ({"spent_result": 1}, {"spent_ns": "-1"}):
            r = evidence.transition(sample(5, 100), dict(sample(5, 100, 2), **patch))
            self.assertFalse(r["direct_valid"])
            self.assertIsNone(r["direct_delta_ns"])

    def test_reset_is_not_continuity(self) -> None:
        r = evidence.transition(sample(245, 23176), dict(sample(245, 0, 0), boot_id="other"))
        self.assertTrue(r["counter_decreased"])
        self.assertIsNone(r["elapsed_s"])
        self.assertIsNone(r["excessive_jump"])

    def test_capture_is_opt_in_in_unified_build(self) -> None:
        self.assertNotIn("pctl-capture-package", package_remote.container_command())
        command = package_remote.container_command(with_pctl_capture=True)
        self.assertIn("pctl-capture-package", command)
        self.assertIn("eden-test-nro", command)
        self.assertIn("test", command)
        self.assertIn("pctl-capture-package", package_remote.ssh_command(with_pctl_capture=True)[-1])

    def test_capture_zip_cannot_activate_anything(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "capture.zip"
            with zipfile.ZipFile(path, "w") as z:
                z.writestr(capture.STAGE + "/staged-exefs.nsp", b"payload")
                z.writestr(capture.STAGE + "/capture-build.json", "{}")
                z.writestr("atmosphere/contents/4200000000BD2300/flags/boot2.flag", b"")
            with self.assertRaisesRegex(ValueError, "inert staging"):
                capture.verify(path)

    def test_internal_tap_has_no_write_power_or_history_services(self) -> None:
        source = (capture.ROOT / "sysmodule/pctl_usage_tap.c").read_text(encoding="utf-8")
        for prohibited in ("pdmqry", "pmshell", "bpc", "pctlStart", "pctlStop", "195101", "1451,", "1452,"):
            self.assertNotIn(prohibited, source)
        self.assertIn("serviceClose(&service);serviceClose(&factory)", source)

    def test_build_id_normalization_cannot_hide_code_or_layout_changes(self) -> None:
        import struct
        note_header = struct.pack("<III", 4, 20, 3) + b"GNU\0"
        def normalize(build_id: bytes, code: bytes = b"code", layout_byte: int = 0) -> bytes:
            main = bytearray(0x100)
            main[0x40:0x54] = build_id
            main[0x14] = layout_byte
            with mock.patch.object(capture, "nsp_member", return_value=bytes(main)), mock.patch.object(capture, "nso_segments_from_nsp", return_value=code+note_header+build_id):
                return capture.normalized_segments(b"fixture")
        self.assertEqual(normalize(b"a"*20), normalize(b"b"*20))
        self.assertNotEqual(normalize(b"a"*20), normalize(b"a"*20, code=b"other"))
        self.assertNotEqual(normalize(b"a"*20), normalize(b"a"*20, layout_byte=1))
        with mock.patch.object(capture, "nsp_member", return_value=bytes(0x100)), mock.patch.object(capture, "nso_segments_from_nsp", return_value=b"unbound note"):
            with self.assertRaisesRegex(ValueError, "no normalization"):
                capture.normalized_segments(b"fixture")

    def test_restore_does_not_overwrite_another_capture_transaction(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            original = b"original"
            (out / "original-exefs.nsp").write_bytes(original)
            d = mock.Mock()
            d.read.return_value = json.dumps({"run_id": "other"}).encode()
            with self.assertRaisesRegex(RuntimeError, "another transaction"):
                live.restore(d, out, {"original_sha256": live.sha(original), "run_id": "ours"})
            d.write.assert_not_called()

    def test_stage_requires_actual_installed_baseline_match(self) -> None:
        d = mock.Mock()
        d.read.return_value = b"original"
        with tempfile.TemporaryDirectory() as root, mock.patch.object(live.pkg, "verify", return_value={"installed_nsp_sha256": "wrong"}):
            with self.assertRaisesRegex(RuntimeError, "preimage"):
                live.stage(d, Path(root), Path("unused"))
            d.write.assert_not_called()

    def test_capture_normalization_preserves_negative_remaining(self) -> None:
        import struct
        words = [0]*34
        words[15:18] = [0x600, 0x100, 125]
        raw = dict(sample(125, 23176), settings_result=0, settings_hex=struct.pack("<34H", *words).hex(), day_index=2477)
        s = live.normalized_sample(raw)
        self.assertEqual(s["configured_minutes"], 125)
        self.assertEqual(s["remaining_ns"], str(-15676*10**9))
        self.assertTrue(evidence.transition(s, s)["allowance_relation_after"])
        self.assertEqual(raw["settings_hex"], s["settings_hex"])

    def test_capture_empty_or_failed_settings_does_not_invent_quota(self) -> None:
        raw = {"settings_result": 0, "settings_hex": bytes(68).hex(), "day_index": 2477}
        self.assertEqual(live.normalized_sample(raw)["settings_mode"], "unconfigured")
        for patch in ({"settings_result": 123}, {"settings_hex": "bad"}):
            self.assertFalse(live.normalized_sample(dict(raw, **patch))["configured_available"])

    def test_standard_mutation_is_published_once_and_acknowledged(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            (out / "capture-install.json").write_text("{}", encoding="utf-8")
            d = mock.Mock()
            task = scenarios.Mainline(d, out)
            rid = "a"*32
            response = {"request_id": rid, "type": "add_today_minutes", "status": "ok"}
            with mock.patch.object(scenarios.uuid, "uuid4", return_value=mock.Mock(hex=rid)), mock.patch.object(scenarios, "optional", return_value=json.dumps(response).encode()):
                self.assertEqual(task.request("add_today_minutes", {"minutes": 120}), response)
            d.put_json.assert_called_once()
            self.assertEqual(task.operations[0]["outcome"], "acknowledged")

    def test_unknown_standard_mutation_is_not_replayed(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            (out / "capture-install.json").write_text("{}", encoding="utf-8")
            d = mock.Mock()
            task = scenarios.Mainline(d, out)
            with mock.patch.object(scenarios.time, "monotonic", side_effect=[0, 31]):
                with self.assertRaisesRegex(TimeoutError, "never resend"):
                    task.request("add_today_minutes", {"minutes": 120})
            d.put_json.assert_called_once()
            self.assertEqual(task.operations[0]["outcome"], "unknown_no_replay")

    def test_rejected_standard_mutation_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            (out / "capture-install.json").write_text("{}", encoding="utf-8")
            task = scenarios.Mainline(mock.Mock(), out)
            rid = "a"*32
            response = {"request_id": rid, "type": "add_today_minutes", "status": "error", "error": {"reason": "bedtime_active"}}
            with mock.patch.object(scenarios.uuid, "uuid4", return_value=mock.Mock(hex=rid)), mock.patch.object(scenarios, "optional", return_value=json.dumps(response).encode()):
                with self.assertRaisesRegex(RuntimeError, "refused"):
                    task.request("add_today_minutes", {"minutes": 120})
            self.assertEqual(task.operations[0]["result"], response)

    def test_followup_status_preserves_existing_scenario_journal(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            out = Path(root)
            (out / "capture-install.json").write_text("{}", encoding="utf-8")
            prior = {"operations": [{"request": {"type": "add_today_minutes"}, "outcome": "acknowledged"}], "phases": [{"name": "measured", "status": "captured"}]}
            (out / "mainline-run.json").write_text(json.dumps(prior), encoding="utf-8")
            task = scenarios.Mainline(mock.Mock(), out)
            task.checkpoint()
            self.assertEqual(json.loads((out / "mainline-run.json").read_text(encoding="utf-8")), prior)
            self.assertEqual(task.operation_start, 1)


if __name__ == "__main__":
    unittest.main()
