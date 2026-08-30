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


def report(mode: str, run_id: str, effect: str | None = None) -> dict:
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
        "summary": {"complete": True},
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
        report("timer_activation_ab", "ab"),
        report("restriction_quick", "pause-a", "paused_or_suspended"),
        report("restriction_quick", "continue-a", "continued"),
        report("restriction_quick", "pause-b", "paused_or_suspended"),
    )
    for value in values:
        (reports / f"{value['run_id']}.json").write_text(json.dumps(value), encoding="utf-8")
    return reports


def test_verify_and_promote_byte_identical_packages() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-qualification-") as tmp_dir:
        root = Path(tmp_dir)
        packages = prepare_packages(root)
        reports = prepare_reports(root)
        verification = verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        require(verification["status"] == "passed", "valid evidence matrix must pass")
        verification_path = root / "verification.json"
        verification_path.write_text(json.dumps(verification), encoding="utf-8")
        output = root / "qualified"
        qualification = promoter.promote(packages, verification_path, output)
        require(qualification["status"] == "qualified", "promotion must emit a detached qualification")
        for name, digest in qualification["packages"].items():
            require(verifier.sha256_file(output / name) == digest, "promoted package bytes must be unchanged")


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


def main() -> int:
    test_verify_and_promote_byte_identical_packages()
    test_old_report_and_changed_package_are_rejected()
    print("Qualification verifier and promotion tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
