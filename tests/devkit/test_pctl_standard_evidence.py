from __future__ import annotations

from copy import deepcopy
import hashlib
import ftplib
import json
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import pctl_standard_evidence as evidence
import pctl_usage_qualification as usage
import test_qualification_tools as fixtures
from test_pctl_usage_qualification import set_contract
from test_package_remote import write_package


CONFIG = {"schema_version": 1, "run_id": "0" * 32, "commit": "a" * 40,
          "release_id": "playwise-test+aaaaaaaaaaaa", "duration_s": 300}


def rewrite_standard(packages: Path, mutate) -> None:
    path = packages / evidence.qualification.STANDARD_ZIP
    with zipfile.ZipFile(path) as archive:
        members = {name: archive.read(name) for name in archive.namelist()}
    mutate(members)
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)


def prepare_packages(root: Path) -> Path:
    packages = fixtures.prepare_packages(root)
    set_contract(packages)
    manifest_path = packages / "playwise/switch/playwise/build.json"
    manifest = json.loads(manifest_path.read_bytes())
    original = write_package(packages / evidence.qualification.STANDARD_ZIP, True)
    def replace(members):
        old = json.dumps(original, ensure_ascii=True, separators=(",", ":")).encode()
        new = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode()
        members["switch/playwise/build.json"] = new
        for name in evidence.qualification.RELEASE_COMPONENTS:
            members[name] = members[name].replace(old, new)
        members["switch/playwise/package-artifacts.json"] = json.dumps({
            "schema_version": 1, "release_id": manifest["release_id"],
            "artifacts": {name: {"size": len(members[name]), "sha256": hashlib.sha256(members[name]).hexdigest()}
                          for name in evidence.qualification.RELEASE_COMPONENTS}}).encode()
    rewrite_standard(packages, replace)
    # Keep the full qualification fixture internally consistent before each test.
    with zipfile.ZipFile(packages / evidence.qualification.COMPLETE_ZIP, "w") as archive:
        archive.writestr(evidence.qualification.STANDARD_ZIP,
                         (packages / evidence.qualification.STANDARD_ZIP).read_bytes())
    return packages


def test_standard_candidate_scope() -> None:
    for optional in ("absent", "stale"):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); packages = prepare_packages(root)
            if optional == "absent":
                (packages / evidence.qualification.LAB_ZIP).unlink()
                (packages / evidence.qualification.COMPLETE_ZIP).unlink()
            else:
                path = packages / "playwise-device-lab/switch/playwise-device-lab/build.json"
                value = json.loads(path.read_bytes()); value["commit"] = "b" * 40
                path.write_text(json.dumps(value), encoding="utf-8")
            plan = evidence.prepare(packages, root / "batch", 300)
            assert plan["identity_scope"] == evidence.IDENTITY_SCOPE and plan["qualified"] is False
            assert set(plan["zip_sha256"]) == {evidence.qualification.STANDARD_ZIP}
            assert set(plan["release_components"]) == set(evidence.qualification.RELEASE_COMPONENTS)
            try:
                evidence.qualification.package_identity(packages)
            except evidence.qualification.QualificationError:
                pass
            else:
                raise AssertionError("full qualification accepted absent or stale optional packages")
    for mode in ("dirty", "profile", "manifest", "component", "embedded", "duplicate", "commit"):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); packages = prepare_packages(root)
            manifest_path = packages / "playwise/switch/playwise/build.json"
            manifest = json.loads(manifest_path.read_bytes())
            if mode in ("dirty", "profile", "commit"):
                if mode == "dirty": manifest["build"]["source_dirty"] = True
                if mode == "profile": manifest["profile"] = "device-lab"
                if mode == "commit": manifest["commit"] = "short"
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            elif mode == "duplicate":
                import warnings
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore", UserWarning)
                    with zipfile.ZipFile(packages / evidence.qualification.STANDARD_ZIP, "a") as archive:
                        archive.writestr("switch/playwise/build.json", json.dumps(manifest))
            else:
                def corrupt(members):
                    if mode == "manifest": members["switch/playwise/build.json"] = b'{}'
                    else:
                        name = "switch/playwise/pctc.nro"
                        members[name] = members[name].replace(b'"profile":"release"', b'"profile":"tampered"')
                        if mode == "embedded":
                            artifacts = json.loads(members["switch/playwise/package-artifacts.json"])
                            artifacts["artifacts"][name] = {"size": len(members[name]),
                                "sha256": hashlib.sha256(members[name]).hexdigest()}
                            members["switch/playwise/package-artifacts.json"] = json.dumps(artifacts).encode()
                rewrite_standard(packages, corrupt)
            try:
                evidence.prepare(packages, root / "bad", 300)
            except evidence.qualification.QualificationError:
                pass
            else:
                raise AssertionError("unsafe standard candidate accepted: " + mode)
            assert not (root / "bad").exists()


def raw_rows() -> list[dict]:
    start = {"schema_version": 1, "kind": "usage_evidence_start", **{k: CONFIG[k] for k in
             ("run_id", "commit", "release_id")}, "boot_id": "1" * 16,
             "started_ns": "1", "duration_ns": "300000000000", "interval_ns": "5000000000"}
    sample = {"schema_version": 1, "kind": "usage_evidence_sample", "source": "pctl_1952",
              "scope": "console_usable_time", "run_id": CONFIG["run_id"], "boot_id": "1" * 16,
              "valid": True, "begin_ns": "1000000000", "end_ns": "1100000000",
              "unix_before": 1791914860, "unix_after": 1791914860, "day_before": 2478, "day_after": 2478,
              "clock_before_valid": True, "clock_after_valid": True, "start_timer_calls": "2",
              **{key: 0 for key in evidence.RESULT_FIELDS}, "timer_enabled": True, "restricted": True,
              "temporarily_unlocked": False, "signed_remaining_ns": "-591000000000",
              "total_ns": "4191000000000", "settings_hex": "01" * 68}
    return [start, sample, {"schema_version": 1, "kind": "usage_evidence_stop", "reason": "lease_expired",
                            "samples": 1, "consecutive_failures": 0}]


def encode(rows: list[dict]) -> bytes:
    return ("\n".join(json.dumps(r) for r in rows) + "\n").encode()


def rejected(rows: list[dict], config: dict = CONFIG) -> None:
    try:
        evidence.audit(encode(rows), config)
    except (usage.UsageQualificationError, evidence.qualification.QualificationError):
        return
    raise AssertionError("malformed or misbound raw accepted")


def test_audit() -> None:
    data = encode(raw_rows())
    result = evidence.audit(data, CONFIG)
    assert result["qualified"] is False and result["stop_record_present"]
    assert result["raw_sha256"] == hashlib.sha256(data).hexdigest()
    assert result["samples"][0]["raw"]["signed_remaining_ns"] == "-591000000000"
    tail = encode(raw_rows()[:2]) + b'{"unclosed":'
    result = evidence.audit(tail, CONFIG)
    assert result["partial_tail_lines"] == [3] and not result["stop_record_present"]
    for field in evidence.RESULT_FIELDS:
        rows = raw_rows(); rows[1][field] = 777; rows[1]["valid"] = False
        assert evidence.audit(encode(rows), CONFIG)["invalid_sample_lines"] == [2]
        rows[1]["valid"] = True; rejected(rows)
    for key, value in (("boot_id", "2" * 16), ("run_id", "3" * 32), ("timer_result", False),
                       ("settings_hex", "01"), ("day_after", 2479), ("total_ns", "-1"),
                       ("end_ns", "3100000001"), ("clock_after_valid", False)):
        rows = raw_rows(); rows[1][key] = value; rejected(rows)
    rows = raw_rows(); rows[0]["commit"] = "b" * 40; rejected(rows)
    rows = raw_rows(); rows[2]["samples"] = 2; rejected(rows)
    rows = raw_rows(); rows.append(deepcopy(rows[1])); rejected(rows)
    rows = raw_rows(); rows.insert(2, deepcopy(rows[1])); rows[-1]["samples"] = 2; rejected(rows)
    rows = raw_rows(); later = deepcopy(rows[1]); later.update(begin_ns="6000000000", end_ns="6100000000", start_timer_calls="1")
    rows.insert(2, later); rows[-1]["samples"] = 2; rejected(rows)
    try:
        evidence.audit(data.replace(b'"valid": true', b'"valid": true, "valid": true'), CONFIG)
    except evidence.qualification.QualificationError:
        pass
    else:
        raise AssertionError("duplicate raw key accepted")
    rows = raw_rows()
    rows[0]["append_diagnostics_version"] = 1
    rows[1]["sample_index"] = 1
    rows[-1].update(save_failures=0, last_failed_sample=0, sample_open_retries=2,
                    last_save_errno=0, last_save_result=0, last_save_operation="none")
    assert evidence.audit(encode(rows), CONFIG)["stop_record_present"]
    rows[1]["sample_index"] = 2; rejected(rows)
    rows[1]["sample_index"] = True; rejected(rows)
    rows[1]["sample_index"] = 1
    rows[-1].update(save_failures=1, last_failed_sample=1, last_save_errno=5,
                    last_save_result=1234, last_save_operation="close")
    rejected(rows)  # A failed close must not become a passed stream because bytes are visible.
    rows[0]["append_diagnostics_version"] = True; rejected(rows)


def test_prepare_and_collect() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        packages = prepare_packages(root)
        out = root / "batch"
        plan = evidence.prepare(packages, out, 300)
        assert plan["manual_observations"] is None and plan["report_status"] == "prepared_not_deployed"
        assert len(plan["config"]["run_id"]) == 32
        try:
            evidence.prepare(packages, out, 300)
        except usage.UsageQualificationError:
            pass
        else:
            raise AssertionError("overwrote existing batch")
        for duration in (True, 299, 7201):
            try:
                evidence.prepare(packages, root / "bad", duration)
            except usage.UsageQualificationError:
                pass
            else:
                raise AssertionError("unbounded lease accepted")
        rows = raw_rows(); rows[0].update({k: plan["config"][k] for k in ("run_id", "commit", "release_id")})
        rows[1]["run_id"] = plan["config"]["run_id"]
        data = encode(rows)
        class Device:
            def __enter__(self): return self
            def __exit__(self, *args): return None
            def ftp(self): return self
            def nlst(self, path):
                assert path == evidence.REMOTE
                return [f'usage-evidence-{plan["config"]["run_id"]}-{"1" * 16}.jsonl', "unrelated.jsonl"]
            def read(self, path):
                assert path.startswith(evidence.REMOTE + "/usage-evidence-")
                return data
        first = evidence.collect(Device(), out, plan)
        assert len(first) == 1 and first[0].read_bytes() == data
        assert evidence.collect(Device(), out, plan) == first
        before = first[0].read_bytes()
        data += b'{"partial"'
        second = evidence.collect(Device(), out, plan)
        assert second[0] != first[0] and first[0].read_bytes() == before
        assert usage.read_json(second[0].with_suffix(".audit.json"))["partial_tail_lines"] == [4]


def test_inactive_stage() -> None:
    class Device:
        def __init__(self, components: dict, mode: str = "ok"):
            self.mode = mode
            self.files = {"/" + name: b"original-live-component" for name in components}
            self.files.update({"/switch/playwise/build.json": b"original-live-manifest",
                               "/switch/playwise/rules.json": b"original-public-rules",
                               "/switch/playwise/handover/runtime-ready.json": b"original-runtime"})
            self.before = dict(self.files)
            self.writes = []
        def __enter__(self): return self
        def __exit__(self, *args): return None
        def ftp(self): return self
        def nlst(self, path):
            assert path.startswith(evidence.STAGING + "/")
            if self.mode == "existing": return []
            if self.mode == "ambiguous": raise ftplib.error_temp("450 Resource busy")
            raise ftplib.error_temp("450 No such file or directory")
        def read(self, path): return self.files[path]
        def write(self, path, data):
            assert path.startswith(evidence.STAGING + "/") and path not in self.files
            self.writes.append(path)
            self.files[path] = b"corrupt" if self.mode == "readback" else data
            if self.mode == "timeout": raise TimeoutError("server may have saved the file; original outcome unknown")
            if self.mode == "live_change": self.files["/switch/playwise/rules.json"] = b"changed externally"
    for mode in ("ok", "ok_absent", "ok_stale", "old_plan", "existing", "ambiguous", "readback",
                 "live_change", "timeout", "plan_changed", "candidate_changed", "scope_changed"):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); packages = prepare_packages(root)
            out = root / "batch"; plan = evidence.prepare(packages, out, 300)
            device = Device(plan["release_components"], mode)
            if mode == "ok_absent":
                (packages / evidence.qualification.LAB_ZIP).unlink()
                (packages / evidence.qualification.COMPLETE_ZIP).unlink()
            if mode == "ok_stale":
                path = packages / "playwise-device-lab/switch/playwise-device-lab/build.json"
                path.write_text('{}', encoding="utf-8")
            if mode == "old_plan":
                plan.pop("identity_scope")
                _, _, plan["zip_sha256"], _ = evidence.qualification.package_identity(packages)
            if mode == "plan_changed": plan["zip_sha256"][evidence.qualification.STANDARD_ZIP] = "0" * 64
            if mode == "candidate_changed":
                rewrite_standard(packages, lambda members: members.update({"switch/playwise/pctc.nro": b"changed"}))
            if mode == "scope_changed": plan["identity_scope"] = "unknown_scope"
            if mode not in ("ok", "ok_absent", "ok_stale", "old_plan"):
                try:
                    evidence.stage(device, out, plan)
                except (usage.UsageQualificationError, evidence.qualification.QualificationError, TimeoutError):
                    pass
                else:
                    raise AssertionError("unsafe staging condition accepted: " + mode)
                if mode in ("existing", "ambiguous", "plan_changed", "candidate_changed", "scope_changed"):
                    assert not device.writes and not (out / "stage-install.json").exists()
                else:
                    journal = usage.read_json(out / "stage-install.json")
                    assert journal["phase"] == "staging_requires_inspection_no_replay"
                    if mode == "timeout":
                        assert len(device.writes) == 1 and len(journal["writes"]) == 1
                        assert journal["writes"][0]["outcome"] == "intent_no_replay"
                        assert device.files[device.writes[0]] != b"corrupt"
                        assert all(device.files[k] == v for k, v in device.before.items())
                        original_bytes = (out / "stage-events.jsonl").read_bytes()
                        try:
                            evidence.stage(device, out, plan)
                        except usage.UsageQualificationError:
                            pass
                        else:
                            raise AssertionError("unknown original write was replayed")
                        assert len(device.writes) == 1
                        assert (out / "stage-events.jsonl").read_bytes() == original_bytes
                continue
            result = evidence.stage(device, out, plan)
            assert result["phase"] == "inactive_staging_verified" and len(device.writes) == 5
            assert result["live_before_sha256"] == result["live_after_sha256"]
            assert not result["candidate_loaded"] and not result["capture_armed"] and not result["qualified"]
            assert all(device.files[k] == v for k, v in device.before.items())
            before = len(device.writes)
            try:
                evidence.stage(device, out, plan)
            except usage.UsageQualificationError:
                pass
            else:
                raise AssertionError("existing staging transaction replayed")
            assert len(device.writes) == before


def test_rejected_capture_preserves_raw() -> None:
    class Device:
        def __init__(self, run_id: str, data: bytes): self.run_id, self.data = run_id, data
        def __enter__(self): return self
        def __exit__(self, *args): return None
        def ftp(self): return self
        def nlst(self, path):
            assert path == evidence.REMOTE
            return [f'usage-evidence-{self.run_id}-{"1" * 16}.jsonl']
        def read(self, path):
            assert path.startswith(evidence.REMOTE + "/usage-evidence-")
            return self.data
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory); packages = prepare_packages(root)
        out = root / "batch"; plan = evidence.prepare(packages, out, 300)
        rows = raw_rows()
        rows[0].update({k: plan["config"][k] for k in ("run_id", "commit", "release_id")})
        rows[1]["run_id"] = plan["config"]["run_id"]
        valid = encode(rows)
        wrong_identity = deepcopy(rows); wrong_identity[0]["commit"] = "b" * 40
        legacy = [{"schema_version": 1, "capture_marker": "PCTL1952_CAPTURE_ONLY", "tap_read_only": True}]
        failures = [encode(wrong_identity), encode(legacy), valid.replace(b'"valid": true', b'"valid": true, "valid": true'),
                    encode(rows[:2]) + b'{"not_utf8":"\xff"}\n']
        original_snapshots = {}
        for data in failures:
            try:
                evidence.collect(Device(plan["config"]["run_id"], data), out, plan)
            except (usage.UsageQualificationError, evidence.qualification.QualificationError, UnicodeError):
                pass
            else:
                raise AssertionError("unqualified or malformed capture accepted")
            files = list((out / "raw").glob("*.jsonl"))
            assert any(p.read_bytes() == data for p in files), "audit failure discarded original raw bytes"
            for p in files:
                original_snapshots.setdefault(p, p.read_bytes())
                assert p.read_bytes() == original_snapshots[p]
                assert not p.with_suffix(".audit.json").exists()
        snapshots = evidence.collect(Device(plan["config"]["run_id"], valid), out, plan)
        assert len(snapshots) == 1
        audit = usage.read_json(snapshots[0].with_suffix(".audit.json"))
        assert audit["qualified"] is False and audit["stop_record_present"]
        assert len(list((out / "raw").glob("*.jsonl"))) == len(failures) + 1
        assert all(p.read_bytes() == data for p, data in original_snapshots.items())


if __name__ == "__main__":
    test_standard_candidate_scope()
    test_audit()
    test_prepare_and_collect()
    test_inactive_stage()
    test_rejected_capture_preserves_raw()
    print("PASS production evidence plan, raw identity/errors, signed precision and immutable read-only pulls")
