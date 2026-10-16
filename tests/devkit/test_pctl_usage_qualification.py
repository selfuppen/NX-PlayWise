from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import tools.qualification.pctl_usage_qualification as usage  # noqa: E402
import tools.qualification.promote_qualified_build as promoter  # noqa: E402
import tools.qualification.verify_device_qualification as verifier  # noqa: E402
import test_qualification_tools as fixtures  # noqa: E402


def set_contract(packages: Path, contract: str = usage.CONTRACT) -> None:
    build_path = packages / "playwise/switch/playwise/build.json"
    manifest = json.loads(build_path.read_text(encoding="utf-8"))
    manifest["usage_contract"] = contract
    build_path.write_text(json.dumps(manifest), encoding="utf-8")
    archive_path = packages / verifier.STANDARD_ZIP
    with zipfile.ZipFile(archive_path) as archive:
        members = {name: archive.read(name) for name in archive.namelist()}
    members["switch/playwise/build.json"] = json.dumps(manifest).encode("utf-8")
    with zipfile.ZipFile(archive_path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(packages / verifier.COMPLETE_ZIP, "w") as archive:
        archive.writestr(verifier.STANDARD_ZIP, archive_path.read_bytes())


def sample(phase: str, seconds: int, used: int, mode: str = "limit", minutes: int = 60,
           business_minutes: int | None = None, reasons: tuple[str, ...] = ()) -> dict:
    unlimited = business_minutes == 0
    return {"phase": phase, "source": "pctl_1952", "scope": "console_usable_time", "ipc_result": 0,
            "boot_id": "fixture-boot", "day_index": 2380, "captured_at": 1000 + seconds,
            "monotonic_ns": str((100 + seconds) * 1_000_000_000), "total_ns": str(used * 1_000_000_000),
            "pairing_span_ns": 100_000_000, "start_timer_calls": 0, "pctl_mode": mode,
            "timer_enabled": mode != "blocked", "temporarily_unlocked": False,
            "configured_minutes": minutes, "business_mode": "unlimited" if unlimited else "limit",
            "business_minutes": minutes if business_minutes is None else business_minutes,
            "signed_remaining_ns": str((minutes * 60 - used) * 1_000_000_000) if mode == "limit" else None,
            "restriction_reasons": list(reasons)}


def prepare_usage(packages: Path, reports: Path) -> dict:
    manifest, _, hashes, components = verifier.package_identity(packages)
    report = usage.template(manifest, hashes[verifier.STANDARD_ZIP], components, "mariko-oled", "22.5.0", "1.11.2")
    report["report_status"] = "final"
    report["observations"] = {name: True for name in usage.OBSERVATIONS}
    report["attestation"] = {"operator": "fixture-operator", "signed_at": 2000}
    report["restoration"] = {"settings_before_hex": "01" * 68, "settings_restored_hex": "01" * 68,
                             "timer_before": True, "timer_after": True, "unexpected_raw_offsets": []}
    values = {
        "usage_live": [sample("before", 0, 10), sample("after", 60, 70)],
        "grant_recovery": [sample("before", 0, 4191, reasons=("daily_allowance",)),
                           sample("after", 1, 4192, minutes=85), sample("active_end", 61, 4252, minutes=85)],
        "proxy_cycle": [sample(phase, i, 100 + i, mode=mode, minutes=1440 if mode == "limit" else 0,
                               business_minutes=0, reasons=("eye_care",) if mode == "blocked" else ())
                        for i, (phase, mode) in enumerate(zip(usage.CASES["proxy_cycle"],
                                                             ("unlimited", "limit", "blocked", "limit", "unlimited")))],
        "reason_handover": [sample("both_blocked", 0, 100, mode="blocked", minutes=0, business_minutes=0,
                                   reasons=("eye_care", "dock")),
                            sample("eye_removed", 1, 100, mode="blocked", minutes=0, business_minutes=0, reasons=("dock",)),
                            sample("released", 2, 101, minutes=1440, business_minutes=0)],
    }
    for case, samples in values.items():
        raw_path = reports / f"{case}-raw.log"
        raw_path.write_text(f"fixture only: {case}; never device evidence", encoding="utf-8")
        data = {"schema_version": 1, "kind": "pctl_usage_samples", "subject": report["subject"],
                "release_components": components, "samples": samples,
                "raw_sources": [{"path": raw_path.name, "sha256": usage.sha256_file(raw_path)}]}
        if case == "grant_recovery":
            data.update(grant_minutes=15, effective_minutes=15)
        path = reports / report["cases"][case]["path"]
        path.write_text(json.dumps(data), encoding="utf-8")
        report["cases"][case]["sha256"] = usage.sha256_file(path)
    save_report(reports, report)
    return report


def save_report(reports: Path, report: dict) -> None:
    (reports / usage.REPORT_NAME).write_text(json.dumps(report), encoding="utf-8")


def expect_rejected(packages: Path, reports: Path, label: str) -> None:
    try:
        verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
    except verifier.QualificationError:
        return
    raise AssertionError(label)


def test_gate_and_promotion() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-usage-gate-") as tmp:
        root = Path(tmp)
        packages = fixtures.prepare_packages(root)
        reports = fixtures.prepare_reports(root)
        set_contract(packages)
        expect_rejected(packages, reports, "new candidate must not qualify using old capability reports alone")
        report = prepare_usage(packages, reports)
        result = verifier.verify(packages, reports, "oled", "22.5.0", "1.11.2")
        assert set(usage.CHECKS) <= set(result["checks"])
        assert len(result["pctl_usage"]["evidence"]) == 4
        verification = root / "verification.json"
        verification.write_text(json.dumps(result), encoding="utf-8")
        qualified = promoter.promote(packages, verification, root / "qualified")
        assert qualified["pctl_usage"] == result["pctl_usage"]
        assert all(verifier.sha256_file(root / "qualified" / name) == digest
                   for name, digest in result["artifacts"]["packages"].items())
        # Unchanged Zip bytes cannot make modified evidence or a stripped gate pass.
        evidence = reports / report["cases"]["grant_recovery"]["path"]
        evidence.write_text(evidence.read_text(encoding="utf-8") + " ", encoding="utf-8")
        try:
            promoter.promote(packages, verification, root / "rejected")
        except promoter.PromotionError:
            assert not (root / "rejected").exists()
        else:
            raise AssertionError("promotion must recheck retained evidence before copying")
        prepare_usage(packages, reports)
        raw = reports / "usage_live-raw.log"
        raw.write_text("tampered fixture", encoding="utf-8")
        try:
            promoter.promote(packages, verification, root / "raw-rejected")
        except promoter.PromotionError:
            assert not (root / "raw-rejected").exists()
        else:
            raise AssertionError("promotion must recheck retained raw files")
        prepare_usage(packages, reports)
        stripped = deepcopy(result)
        stripped.pop("pctl_usage")
        verification.write_text(json.dumps(stripped), encoding="utf-8")
        try:
            promoter.promote(packages, verification, root / "stripped")
        except promoter.PromotionError:
            assert not (root / "stripped").exists()
        else:
            raise AssertionError("declared usage contract must survive promotion")


def test_reports_and_samples_fail_closed() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-usage-gate-") as tmp:
        root = Path(tmp)
        packages = fixtures.prepare_packages(root)
        reports = fixtures.prepare_reports(root)
        set_contract(packages)
        original = prepare_usage(packages, reports)
        changes = (
            lambda r: r.update(report_status="draft"),
            lambda r: r.update(usage_contract="pctl1952_without_proxy"),
            lambda r: r["subject"].update(commit="b" * 40),
            lambda r: r.update(standard_zip_sha256="0" * 64),
            lambda r: r["environment"].update(hos="other"),
            lambda r: r["observations"].update(proxy_cycle_observed=None),
            lambda r: r["attestation"].update(operator=None),
            lambda r: r["restoration"].update(timer_after=False),
            lambda r: r["restoration"].update(settings_restored_hex="00" * 68),
            lambda r: r["cases"].pop("proxy_cycle"),
            lambda r: r["cases"]["usage_live"].update(path="../outside.json"),
        )
        for change in changes:
            invalid = deepcopy(original)
            change(invalid)
            save_report(reports, invalid)
            expect_rejected(packages, reports, "incomplete, mismatched or unsupported report accepted")
        mutations = (
            ("usage_live", lambda d: d["samples"][1].update(total_ns=d["samples"][0]["total_ns"])),
            ("usage_live", lambda d: d["samples"][1].update(boot_id="reboot")),
            ("usage_live", lambda d: d["samples"][1].update(day_index=2379)),
            ("usage_live", lambda d: d["samples"][1].update(total_ns="86400000000001")),
            ("usage_live", lambda d: d["samples"][0].update(total_ns="01")),
            ("usage_live", lambda d: d["samples"][0].update(ipc_result=True)),
            ("usage_live", lambda d: d["samples"][0].update(pairing_span_ns=3_000_000_000)),
            ("usage_live", lambda d: d.update(raw_sources=[])),
            ("usage_live", lambda d: d["raw_sources"][0].update(path="../outside-raw.log")),
            ("grant_recovery", lambda d: d.update(effective_minutes=25)),
            ("grant_recovery", lambda d: d["samples"][1].update(signed_remaining_ns="0")),
            ("grant_recovery", lambda d: d["samples"][1].update(total_ns="0")),
            ("grant_recovery", lambda d: d["samples"][1].update(timer_enabled=False)),
            ("usage_live", lambda d: d["samples"][1].update(temporarily_unlocked=True)),
            ("reason_handover", lambda d: d["samples"][1].update(pctl_mode="unlimited")),
            ("reason_handover", lambda d: d["samples"][2].update(start_timer_calls=1)),
            ("proxy_cycle", lambda d: [v.update(start_timer_calls=1) for v in d["samples"][3:]]),
        )
        for case, mutate in mutations:
            original = prepare_usage(packages, reports)
            path = reports / original["cases"][case]["path"]
            data = json.loads(path.read_text(encoding="utf-8"))
            mutate(data)
            path.write_text(json.dumps(data), encoding="utf-8")
            original["cases"][case]["sha256"] = usage.sha256_file(path)
            save_report(reports, original)
            expect_rejected(packages, reports, "invalid counter, award, epoch or handover accepted")
        original = prepare_usage(packages, reports)
        path = reports / original["cases"]["usage_live"]["path"]
        text = path.read_text(encoding="utf-8").replace('"ipc_result": 0', '"ipc_result": 0, "ipc_result": 0', 1)
        path.write_text(text, encoding="utf-8")
        original["cases"]["usage_live"]["sha256"] = usage.sha256_file(path)
        save_report(reports, original)
        expect_rejected(packages, reports, "duplicate evidence key accepted")


def test_manifest_binding_and_draft_cli() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-usage-gate-") as tmp:
        root = Path(tmp)
        packages = fixtures.prepare_packages(root)
        reports = fixtures.prepare_reports(root)
        set_contract(packages)
        build = packages / "playwise/switch/playwise/build.json"
        content = build.read_text(encoding="utf-8")
        altered = json.loads(content)
        altered.pop("usage_contract")
        build.write_text(json.dumps(altered), encoding="utf-8")
        expect_rejected(packages, reports, "loose manifest must not strip an embedded usage contract")
        build.write_text(content, encoding="utf-8")
        output = root / "draft.json"
        command = [sys.executable, str(ROOT / "tools/qualification/pctl_usage_qualification.py"), "--packages", str(packages), "--output", str(output)]
        run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
        assert run.returncode == 0, run.stderr
        draft = json.loads(output.read_text(encoding="utf-8"))
        assert draft["report_status"] == "draft" and set(draft["observations"].values()) == {None}
        save_report(reports, draft)
        expect_rejected(packages, reports, "generated draft must not pass qualification")
        before = output.read_bytes()
        assert subprocess.run(command, capture_output=True).returncode != 0
        assert output.read_bytes() == before


def main() -> None:
    test_gate_and_promotion()
    test_reports_and_samples_fail_closed()
    test_manifest_binding_and_draft_cli()
    print("PASS: PCTL1952 qualification identity, continuity, proxy baseline and promotion integrity")


if __name__ == "__main__":
    main()
