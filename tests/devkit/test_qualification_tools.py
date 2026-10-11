#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import promote_qualified_build as promoter  # noqa: E402
import verify_device_qualification as verifier  # noqa: E402
import make_eye_care_qualification_template as make_eye_care  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def manifest(profile: str) -> dict:
    return {
        "schema_version": 1,
        "playwise_version": verifier.VERSION,
        "commit": "a" * 40,
        "release_id": f"playwise-{verifier.VERSION}+aaaaaaaaaaaa",
        "profile": profile,
        "build": {"source_dirty": False, "libnx": "libnx 4.12.0-1", "container_image": "devkitpro:v1"},
        "qualification": {"status": "pending"},
    }


def prepare_packages(root: Path) -> Path:
    packages = root / "packages"
    release_manifest = manifest("release")
    lab_manifest = manifest("device-lab")
    release_build = packages / "playwise" / "switch" / "playwise" / "build.json"
    lab_build = packages / "playwise-device-lab" / "switch" / "playwise-device-lab" / "build.json"
    release_build.parent.mkdir(parents=True)
    lab_build.parent.mkdir(parents=True)
    release_build.write_text(json.dumps(release_manifest), encoding="utf-8")
    lab_build.write_text(json.dumps(lab_manifest), encoding="utf-8")
    with zipfile.ZipFile(packages / verifier.STANDARD_ZIP, "w") as archive:
        for index, member in enumerate(verifier.RELEASE_COMPONENTS):
            archive.writestr(member, f"component-{index}".encode())
    with zipfile.ZipFile(packages / verifier.COMPLETE_ZIP, "w") as archive:
        archive.writestr(verifier.STANDARD_ZIP, b"standard-inner")
    with zipfile.ZipFile(packages / verifier.LAB_ZIP, "w") as archive:
        archive.writestr("DEVICE-LAB.txt", "内部取证工具")
    return packages


def parity() -> dict:
    return {
        "commands": {
            "1006": {"comparable": True, "value_equal": True},
            "1031": {"comparable": True, "value_equal": True},
            "1035": {"comparable": True, "structure_equal": True},
            "1457": {"comparable": True},
            "1458": {"comparable": True, "value_equal": True},
        },
        "settings_0x44": {"same_value_write_succeeded": True, "exactly_restored": True},
    }


def report(mode: str, run_id: str, slot: str, game_slot: str,
           pause_expected: str, effect: str | None = None) -> dict:
    write_phases = (
        ["ab_limited_settings_only", "ab_restriction_settings_only", "ab_grant_settings_only",
         "ab_restriction_before_unlimited", "ab_unlimited_settings_only"]
        if mode == "timer_activation_ab" else ["restriction_effect"]
    )
    settings_hex = "00" * 68
    value = {
        "version": 2,
        "schema_version": 2,
        "run_id": run_id,
        "mode": mode,
        "report_status": "final",
        "entry_method": "hot_switch",
        "campaign": {
            "campaign_id": "campaign-test",
            "slot": slot,
            "attempt": 1,
            "game_slot": game_slot,
            "official_pause_expected": pause_expected,
            "context_confirmed": True,
        },
        "environment": {
            "runtime": {
                "model": "mariko-oled",
                "hos": "22.5.0",
                "atmosphere": True,
                "atmosphere_detection": {"source": "spl:ExosphereApiVersion", "version": "1.11.2"},
            },
            "build": manifest("device-lab"),
        },
        "public_parity": parity(),
        "phases": [
            {
                "phase": phase,
                "before": {"settings_hex": settings_hex},
                "after": {"settings_hex": settings_hex},
                "settings_write_scope": {
                    "comparison": "phase_prewrite_to_after",
                    "prewrite_settings_hex": settings_hex,
                    "unexpected_bytes_unchanged": True,
                },
            }
            for phase in write_phases
        ],
        "restoration": {"proved": True, "verdict": "exact_restore_proved"},
        "summary": {
            "complete": True,
            "activation_evidence_complete": True if mode == "timer_activation_ab" else None,
            "lifecycle_evidence_complete": None,
        },
    }
    if mode == "timer_activation_ab":
        value["baseline"] = {"activation_preconditions_met": True, "remaining_ns": 700, "minimum_remaining_ns": 600}
        value["timer_activation_ab"] = {
            "home_awake_counted": True,
            "sleep_excluded": True,
            "fallback_cases": [
                {"target": target, "settings_only_runtime_ready": True,
                 "fallback_called": False, "fallback_succeeded": None}
                for target in ("limited", "grant", "unlimited")
            ],
        }
        value["manual_observation"] = None
        value["manual_runtime_effect"] = None
    else:
        value["manual_observation"] = "restriction_visible"
        value["manual_runtime_effect"] = effect
    return value


def prepare_reports(root: Path) -> Path:
    reports = root / "reports"
    reports.mkdir()
    values = (
        report("timer_activation_ab", "ab", "timer_activation_ab", "none", "not_applicable"),
        report("restriction_quick", "pause-a", "pause_on_game_a", "a", "on", "paused_or_suspended"),
        report("restriction_quick", "pause-b", "pause_on_game_b", "b", "on", "paused_or_suspended"),
        report("restriction_quick", "continue-b", "pause_off_game_b", "b", "off", "continued"),
    )
    for value in values:
        (reports / f"{value['run_id']}.json").write_text(json.dumps(value), encoding="utf-8")
    (reports / "campaign-test.campaign.json").write_text(json.dumps({
        "version": 1,
        "schema_version": 1,
        "report_status": "final",
        "campaign_id": "campaign-test",
        "state": "complete",
        "original_pause_state": "on",
        "entry_method": "hot_switch",
        "accepted_run_ids": ["ab", "pause-a", "pause-b", "continue-b"],
    }), encoding="utf-8")
    (reports / "bedtime-qualification.json").write_text(json.dumps({
        "schema_version": 1,
        "report_status": "final",
        "subject": {"commit": "a" * 40,
                    "release_id": f"playwise-{verifier.VERSION}+aaaaaaaaaaaa"},
        "environment": {"model": "mariko-oled", "hos": "22.5.0", "atmosphere": "1.11.2"},
        "games": ["game-a", "game-b"],
        "timing": {"boundary_trigger_seconds": 60, "wake_trigger_seconds": 59},
        "blocked_entries": {"game": True, "homebrew": True, "home": True,
                            "system_settings": True, "playwise_nro": True},
        "overlay": {
            "opened_during_popup": True, "pin_verified": True,
            "shared_cooldown_verified": True, "single_action_authorization_verified": True,
            "daily_limit_message_verified": True,
            "recovery": {
                action: {"request_submitted": True, "pctl_reread": True, "popup_cleared": True}
                for action in ("skip_instance", "disable_bedtime", "restore_install_snapshot")
            },
            "backend_failure": {"reported_unconfirmed": True, "external_recovery_shown": True,
                                "startup_restore_flag_created": False},
        },
        "official_pause": {"on_recorded": True, "off_recorded": True,
                           "playwise_modified_setting": False},
        "lifecycle": {phase: True for phase in
                      ("home", "foreground", "suspend", "sleep", "reboot", "cross_day",
                       "manual_clock_change", "recovery_failure")},
        "pctl": {"settings_exactly_restored": True, "unexpected_raw_offsets": [],
                 "bedtime_consumed_daily_allowance": False},
        "missing_overlay": {"warning_unskippable": True, "pin_and_long_hold_required": True,
                            "enable_allowed_after_confirmation": True,
                            "schedule_continues_without_handshake": True},
    }), encoding="utf-8")
    return reports


def test_verify_and_promote_byte_identical_packages() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        (reports / "failed-attempt.json").write_text(json.dumps({"report_status": "final", "mode": "full"}), encoding="utf-8")
        verification = verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        require(verification["status"] == "passed", "valid evidence matrix must pass")
        require(verification["reports"]["run_ids"] == ["ab", "pause-a", "pause-b", "continue-b"],
                "failed attempts and free sessions must not enter the accepted campaign set")
        verification_path = root / "verification.json"
        verification_path.write_text(json.dumps(verification), encoding="utf-8")
        output = root / "qualified"
        qualification = promoter.promote(packages, verification_path, output)
        require(qualification["status"] == "qualified", "promotion must emit a detached qualification")
        for name, digest in qualification["packages"].items():
            require(verifier.sha256_file(output / name) == digest, "promoted package bytes must be unchanged")


def test_manual_status_can_still_enter_full_qualification() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        build_path = packages / "playwise" / "switch" / "playwise" / "build.json"
        build = json.loads(build_path.read_text(encoding="utf-8"))
        build["qualification"].update({"status": "manual_verified", "method": "manual", "scope": "main_features"})
        build_path.write_text(json.dumps(build), encoding="utf-8")
        reports = prepare_reports(root)
        require(verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")["status"] == "passed",
                "manual status must not block later full qualification")


def test_old_report_and_changed_package_are_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        old = json.loads((reports / "ab.json").read_text(encoding="utf-8"))
        old["version"] = 1
        old["schema_version"] = 1
        (reports / "ab.json").write_text(json.dumps(old), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("schema v2" in str(exc), "old schema rejection must be explicit")
        else:
            raise AssertionError("schema v1 report must not qualify")


def test_missing_runtime_identity_is_rejected_explicitly() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        missing_runtime = json.loads((reports / "pause-a.json").read_text(encoding="utf-8"))
        missing_runtime["environment"]["runtime"] = None
        (reports / "pause-a.json").write_text(json.dumps(missing_runtime), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("缺少运行时环境证据" in str(exc), "missing runtime rejection must be explicit")
        else:
            raise AssertionError("report without runtime identity must not qualify")


def test_incomplete_bedtime_recovery_is_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        bedtime_path = reports / "bedtime-qualification.json"
        bedtime = json.loads(bedtime_path.read_text(encoding="utf-8"))
        bedtime["overlay"]["recovery"]["skip_instance"]["popup_cleared"] = False
        bedtime_path.write_text(json.dumps(bedtime), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("skip_instance" in str(exc), "bedtime recovery rejection must name the missing action")
        else:
            raise AssertionError("incomplete bedtime recovery evidence must not qualify")


def test_incomplete_bedtime_timing_is_rejected_cleanly() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        bedtime_path = reports / "bedtime-qualification.json"
        bedtime = json.loads(bedtime_path.read_text(encoding="utf-8"))
        bedtime["timing"]["wake_trigger_seconds"] = None
        bedtime_path.write_text(json.dumps(bedtime), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("60 秒" in str(exc), "incomplete bedtime timing rejection must be explicit")
        else:
            raise AssertionError("incomplete bedtime timing evidence must not qualify")


def test_misleading_evidence_summary_is_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        ab_path = reports / "ab.json"
        misleading = json.loads(ab_path.read_text(encoding="utf-8"))
        misleading["summary"]["activation_evidence_complete"] = None
        ab_path.write_text(json.dumps(misleading), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("A/B 摘要证据状态不一致" in str(exc), "misleading evidence summary rejection must be explicit")
        else:
            raise AssertionError("A/B report with a misleading evidence summary must not qualify")


def test_multiple_campaigns_require_explicit_selection() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        second = json.loads((reports / "campaign-test.campaign.json").read_text(encoding="utf-8"))
        second["campaign_id"] = "campaign-second"
        (reports / "campaign-second.campaign.json").write_text(json.dumps(second), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("--campaign" in str(exc), "multiple campaign rejection must request explicit selection")
        else:
            raise AssertionError("multiple campaigns must not be selected implicitly")
        verification = verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2", "campaign-test")
        require(verification["campaign"]["campaign_id"] == "campaign-test",
                "explicit campaign selection must bind the qualification result")


def prepare_vision_sidecars(reports: Path) -> None:
    """Attach a vision_ai sidecar + screenshot bundle to each restriction_quick
    run, matching the report's observation fields (as the driver would)."""
    specs = {
        "pause-a": ("pause_on_game_a", "restriction_visible", "paused_or_suspended"),
        "pause-b": ("pause_on_game_b", "restriction_visible", "paused_or_suspended"),
        "continue-b": ("pause_off_game_b", "restriction_visible", "continued"),
    }
    for run_id, (slot, observation, effect) in specs.items():
        run_dir = reports / "vision" / run_id
        run_dir.mkdir(parents=True, exist_ok=True)
        shots = []
        for index in range(2):
            shot = run_dir / f"{index:02d}.jpg"
            shot.write_bytes(b"\xff\xd8" + f"{run_id}-{index}".encode("ascii"))
            shots.append({"checkpoint": f"f{index}", "path": shot.relative_to(reports).as_posix(),
                          "sha256": verifier.sha256_file(shot), "captured_at": "t"})
        (reports / f"{run_id}.vision.json").write_text(json.dumps({
            "schema_version": 1, "method": "vision_ai", "run_id": run_id,
            "campaign_id": "campaign-test", "slot": slot,
            "observation": observation, "runtime_effect": effect, "context_confirmed": True,
            "confidence": "high", "reasoning": "frames show the prompt and the game state",
            "sysbotbase_commit": "ae13548", "wire_log_sha256": "deadbeef",
            "screenshots": shots,
        }), encoding="utf-8")


def test_vision_source_requires_and_binds_sidecars() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        # Default (manual) source must pass without any sidecar...
        require(verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")["status"] == "passed",
                "manual observation source must not require vision sidecars")
        # ...but vision_ai must FAIL closed when the sidecars are absent.
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2",
                            observation_source="vision_ai")
        except verifier.QualificationError as exc:
            require("视觉旁证" in str(exc), "vision_ai without a sidecar must fail explicitly")
        else:
            raise AssertionError("vision_ai mode must require a sidecar per restriction run")
        prepare_vision_sidecars(reports)
        result = verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2",
                                 observation_source="vision_ai")
        require(result["status"] == "passed", "valid vision sidecars must qualify")
        require(result["observation_source"] == "vision_ai", "verification records the observation source")
        require("vision_attestation" in result["checks"], "vision check is recorded")
        require(len(result["vision_attestation"]) == 3, "all three restriction slots are attested")


def test_vision_rejects_tampered_screenshot() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        prepare_vision_sidecars(reports)
        (reports / "vision" / "pause-a" / "00.jpg").write_bytes(b"\xff\xd8tampered")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2",
                            observation_source="vision_ai")
        except verifier.QualificationError as exc:
            require("哈希不符" in str(exc), "a tampered screenshot must fail on hash mismatch")
        else:
            raise AssertionError("a tampered screenshot must not qualify")


def test_vision_rejects_missing_screenshot() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        prepare_vision_sidecars(reports)
        (reports / "vision" / "pause-b" / "01.jpg").unlink()
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2",
                            observation_source="vision_ai")
        except verifier.QualificationError as exc:
            require("截图缺失" in str(exc), "a missing screenshot must fail explicitly")
        else:
            raise AssertionError("a missing screenshot must not qualify")


def test_vision_rejects_verdict_mismatch() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        prepare_vision_sidecars(reports)
        sidecar = reports / "continue-b.vision.json"
        verdict = json.loads(sidecar.read_text(encoding="utf-8"))
        verdict["runtime_effect"] = "paused_or_suspended"  # disagree with the report
        sidecar.write_text(json.dumps(verdict), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2",
                            observation_source="vision_ai")
        except verifier.QualificationError as exc:
            require("不一致" in str(exc), "a verdict that disagrees with the report must fail")
        else:
            raise AssertionError("a verdict/report mismatch must not qualify")


def eye_care_report_payload(**overrides) -> dict:
    base = {
        "schema_version": 1,
        "report_status": "final",
        "subject": {"commit": "a" * 40,
                    "release_id": f"playwise-{verifier.VERSION}+aaaaaaaaaaaa"},
        "environment": {"model": "mariko-oled", "hos": "22.5.0", "atmosphere": "1.11.2"},
        "games": ["game-a", "game-b"],
        "timing": {"rest_start_trigger_seconds": 30, "rest_end_trigger_seconds": 20},
        "observations": {key: True for key, _ in make_eye_care.OBSERVATION_FIELDS},
        "pctl": {"settings_exactly_restored": True, "unexpected_raw_offsets": [],
                 "eye_care_consumed_daily_allowance": False},
    }
    base.update(overrides)
    return base


def test_eye_care_gate_passes_when_required() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        (reports / "eye-care-qualification.json").write_text(
            json.dumps(eye_care_report_payload()), encoding="utf-8")
        result = verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2", require_eye_care=True)
        require(result["status"] == "passed", "a complete eye-care report must qualify")
        require("eye_care_rest_cycle" in result["checks"] and "eye_care_isolation" in result["checks"],
                "eye-care checks are recorded when the report is present")
        require("eye_care_path" in result["reports"], "verification binds the eye-care report path")


def test_eye_care_required_but_missing_is_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)  # no eye-care report present
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2", require_eye_care=True)
        except verifier.QualificationError as exc:
            require("eye-care-qualification.json" in str(exc),
                    "a missing eye-care report must be named when it is required")
        else:
            raise AssertionError("--require-eye-care with no report must not qualify")
        # ...but with eye-care NOT required, the same reports still qualify (opt-in gate).
        require(verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")["status"] == "passed",
                "a missing eye-care report must not block the base qualification")


def test_eye_care_present_but_invalid_fails_closed() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        bad = eye_care_report_payload()
        bad["pctl"]["eye_care_consumed_daily_allowance"] = True  # isolation broken
        (reports / "eye-care-qualification.json").write_text(json.dumps(bad), encoding="utf-8")
        # Even without --require-eye-care, a PRESENT report is fail-closed validated.
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        except verifier.QualificationError as exc:
            require("额度隔离" in str(exc), "a present eye-care report that breaks isolation must fail closed")
        else:
            raise AssertionError("an eye-care report that consumes the daily allowance must not qualify")


def test_eye_care_incomplete_observation_is_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        bad = eye_care_report_payload()
        bad["observations"]["disable_eye_care_restores_unlimited"] = False
        (reports / "eye-care-qualification.json").write_text(json.dumps(bad), encoding="utf-8")
        try:
            verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2", require_eye_care=True)
        except verifier.QualificationError as exc:
            require("disable_eye_care_restores_unlimited" in str(exc),
                    "an incomplete eye-care observation must be named")
        else:
            raise AssertionError("an incomplete eye-care observation must not qualify")


def main() -> int:
    test_verify_and_promote_byte_identical_packages()
    test_manual_status_can_still_enter_full_qualification()
    test_old_report_and_changed_package_are_rejected()
    test_missing_runtime_identity_is_rejected_explicitly()
    test_incomplete_bedtime_recovery_is_rejected()
    test_incomplete_bedtime_timing_is_rejected_cleanly()
    test_misleading_evidence_summary_is_rejected()
    test_multiple_campaigns_require_explicit_selection()
    test_vision_source_requires_and_binds_sidecars()
    test_vision_rejects_tampered_screenshot()
    test_vision_rejects_missing_screenshot()
    test_vision_rejects_verdict_mismatch()
    test_eye_care_gate_passes_when_required()
    test_eye_care_required_but_missing_is_rejected()
    test_eye_care_present_but_invalid_fails_closed()
    test_eye_care_incomplete_observation_is_rejected()
    print("Qualification verifier and promotion tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
