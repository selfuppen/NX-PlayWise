"""Prepare, stage and audit production evidence. Never activate, write PCTL or certify."""
from __future__ import annotations

import argparse
import ftplib
import hashlib
import json
from pathlib import Path
import re
import uuid
import zipfile

import pctl_usage_qualification as usage
import verify_device_qualification as qualification
from package_remote import PackageError, verify_package_zip

REMOTE = "/switch/playwise/support"
STAGING = "/switch/playwise-qualification-staging"
RESULT_FIELDS = ("session_result", "timer_result", "remaining_result", "restricted_result",
                 "spent_result", "settings_result", "unlocked_result")
IDENTITY_SCOPE = "standard_candidate_only"


def standard_candidate_identity(packages: Path) -> tuple[dict, dict[str, str], dict[str, str]]:
    """Bind production evidence to one clean standard Zip, without certifying it."""
    manifest = qualification.read_json(packages / "playwise/switch/playwise/build.json")
    qualification.require(manifest.get("profile") == "release", "standard candidate must be release")
    qualification.require(manifest.get("playwise_version") == qualification.VERSION, "candidate version mismatch")
    commit = manifest.get("commit")
    qualification.require(isinstance(commit, str) and re.fullmatch(r"[0-9a-f]{40}", commit),
                          "complete candidate commit required")
    release_id = manifest.get("release_id")
    qualification.require(isinstance(release_id, str) and
                          release_id.startswith(f"playwise-{qualification.VERSION}+{commit[:12]}"),
                          "candidate release/commit mismatch")
    qualification.require(manifest.get("qualification", {}).get("status") in ("pending", "manual_verified"),
                          "candidate status must be pending or manual_verified")
    build = manifest.get("build", {})
    qualification.require(build.get("source_dirty") is False, "candidate contains tracked source changes")
    for key in ("libnx", "container_image"):
        qualification.require(build.get(key) not in (None, "", "unknown"), f"candidate missing {key} identity")
    path = packages / qualification.STANDARD_ZIP
    try:
        # Reuse the public package gate: artifact sizes/digests, embedded frontend
        # manifests, production markers, defaults and safe unique Zip members.
        verify_package_zip(path, "playwise", expected_manifest=manifest)
        with zipfile.ZipFile(path) as archive:
            qualification.unique_archive(archive, path.name)
            qualification.read_json_text(archive.read("switch/playwise/build.json"), path.name)
            qualification.read_json_text(archive.read("switch/playwise/package-artifacts.json"), path.name)
            qualification.require(archive.read("atmosphere/contents/4200000000BD2300/flags/boot2.flag") == b"",
                                  "standard candidate boot2.flag must be empty")
            components = {name: qualification.sha256_bytes(archive.read(name))
                          for name in qualification.RELEASE_COMPONENTS}
    except (OSError, KeyError, ValueError, PackageError, zipfile.BadZipFile) as exc:
        raise qualification.QualificationError(f"invalid standard candidate: {exc}") from exc
    return manifest, {path.name: qualification.sha256_file(path)}, components


def save_new(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def prepare(packages: Path, out: Path, duration_s: int) -> dict:
    usage.integer(duration_s, 300, 7200, "duration_s")
    manifest, hashes, components = standard_candidate_identity(packages)
    usage.require(manifest.get("usage_contract") == usage.CONTRACT, "production usage contract missing")
    usage.require(not out.exists(), "batch directory already exists; preserve previous materials")
    config = {"schema_version": 1, "run_id": uuid.uuid4().hex, "commit": manifest["commit"],
              "release_id": manifest["release_id"], "duration_s": duration_s}
    plan = {"schema_version": 1, "kind": "production_usage_evidence_plan", "config": config,
            "identity_scope": IDENTITY_SCOPE, "qualified": False,
            "packages": str(packages.resolve()), "zip_sha256": hashes, "release_components": components,
            "report_status": "prepared_not_deployed", "manual_observations": None,
            "remote_config_path": REMOTE + "/usage-evidence.json"}
    save_new(out / "plan.json", plan)
    save_new(out / "usage-evidence.json", config)
    return plan


def raw_sample(value: dict) -> bool:
    """Validate wire shape and recompute validity, retaining failed wire values."""
    for key in RESULT_FIELDS:
        usage.integer(value.get(key), 0, (1 << 32) - 1, key)
    for key in ("clock_before_valid", "clock_after_valid", "timer_enabled", "restricted",
                "temporarily_unlocked", "valid"):
        usage.require(type(value.get(key)) is bool, key + ": boolean required")
    begin = usage.nanoseconds(value.get("begin_ns"), False, "begin_ns")
    end = usage.nanoseconds(value.get("end_ns"), False, "end_ns")
    before = usage.integer(value.get("unix_before"), 0, (1 << 63) - 1, "unix_before")
    after = usage.integer(value.get("unix_after"), 0, (1 << 63) - 1, "unix_after")
    day = usage.integer(value.get("day_before"), 0, 65535, "day_before")
    after_day = usage.integer(value.get("day_after"), 0, 65535, "day_after")
    spent = usage.nanoseconds(value.get("total_ns"), True, "total_ns")
    usage.nanoseconds(value.get("signed_remaining_ns"), True, "signed_remaining_ns")
    usage.nanoseconds(value.get("start_timer_calls"), False, "start_timer_calls")
    usage.require(isinstance(value.get("settings_hex"), str) and
                  re.fullmatch(r"[0-9a-f]{136}", value["settings_hex"]), "complete raw 0x44 required")
    usage.require(value.get("source") == "pctl_1952" and value.get("scope") == "console_usable_time",
                  "unexpected consumption source or scope")
    valid = (value["clock_before_valid"] and value["clock_after_valid"] and day == after_day and
             before <= after and 0 <= end - begin <= usage.PAIRING_NS and spent >= 0 and
             all(value[key] == 0 for key in RESULT_FIELDS))
    usage.require(value["valid"] is valid, "raw validity flag contradicts wire/clock results")
    return valid


def audit(data: bytes, config: dict) -> dict:
    rows = data.splitlines(keepends=True)
    samples, damaged, invalid = [], [], []
    start = None
    stopped = False
    previous = None
    gaps = []
    for number, raw in enumerate(rows, 1):
        # A live FTP snapshot may end mid-write. Preserve that exact byte tail.
        if number == len(rows) and not raw.endswith(b"\n"):
            damaged.append(number)
            continue
        value = qualification.read_json_text(raw, f"raw line {number}")
        usage.require(type(value.get("schema_version")) is int and value["schema_version"] == 1,
                      "unexpected evidence schema")
        kind = value.get("kind")
        if kind == "usage_evidence_start":
            usage.require(start is None and number == 1, "repeated or misplaced start record")
            usage.require(all(value.get(key) == config[key] for key in ("commit", "release_id", "run_id")),
                          "production stream identity differs from prepared candidate")
            usage.require(isinstance(value.get("boot_id"), str) and re.fullmatch(r"[0-9a-f]{16}", value["boot_id"]),
                          "production boot identity missing")
            usage.require(value.get("duration_ns") == str(config["duration_s"] * 1_000_000_000) and
                          value.get("interval_ns") == "5000000000", "lease or frequency differs")
            usage.nanoseconds(value.get("started_ns"), False, "started_ns")
            diagnostics = value.get("append_diagnostics_version")
            usage.require(diagnostics is None or (type(diagnostics) is int and diagnostics == 1),
                          "unknown append diagnostics version")
            start = value
        elif kind == "usage_evidence_sample":
            usage.require(start is not None and not stopped, "sample outside started stream")
            usage.require(value.get("run_id") == start["run_id"] and value.get("boot_id") == start["boot_id"],
                          "mixed run/boot identity")
            if start.get("append_diagnostics_version") == 1:
                sequence = usage.integer(value.get("sample_index"), 1, (1 << 32) - 1, "sample_index")
                usage.require(sequence == len(samples) + 1, "sample sequence gap or duplicate; preserve raw")
            else:
                usage.require("sample_index" not in value, "sample sequence missing its declared version")
            valid = raw_sample(value)
            if not valid:
                invalid.append(number)
            if previous:
                delta = int(value["begin_ns"]) - int(previous["begin_ns"])
                usage.require(delta > 0 and int(value["start_timer_calls"]) >= int(previous["start_timer_calls"]),
                              "capture tick or 1451 calls regressed")
                if delta > 300_000_000_000:
                    gaps.append({"before_line": samples[-1]["line"], "after_line": number, "span_ns": str(delta)})
            samples.append({"line": number, "raw": value})
            previous = value
        elif kind == "usage_evidence_stop":
            usage.require(start is not None and not stopped, "misplaced stop record")
            usage.require(value.get("reason") in ("lease_expired", "failure_limit", "config_removed_or_changed",
                                                   "monotonic_regression", "process_handoff"), "unknown stop reason")
            usage.require(type(value.get("samples")) is int and value["samples"] == len(samples), "stop count mismatch")
            usage.integer(value.get("consecutive_failures"), 0, 3, "consecutive_failures")
            if start.get("append_diagnostics_version") == 1:
                save_failures = usage.integer(value.get("save_failures"), 0, value["samples"], "save_failures")
                usage.integer(value.get("sample_open_retries"), 0, (1 << 32) - 1, "sample_open_retries")
                last_failed = usage.integer(value.get("last_failed_sample"), 0, value["samples"], "last_failed_sample")
                last_errno = usage.integer(value.get("last_save_errno"), 0, (1 << 31) - 1, "last_save_errno")
                last_result = usage.integer(value.get("last_save_result"), 0, (1 << 32) - 1, "last_save_result")
                usage.require(value.get("last_save_operation") in ("none", "open", "write", "close"),
                              "unknown append failure operation")
                usage.require(save_failures == 0 and last_failed == 0 and last_errno == 0 and
                              last_result == 0 and value["last_save_operation"] == "none",
                              "sample append failure recorded; preserve raw even when the row is visible")
            stopped = True
        else:
            raise usage.UsageQualificationError("unknown raw record kind")
    usage.require(start is not None, "no complete production start record")
    return {"schema_version": 1, "kind": "production_usage_evidence_audit", "qualified": False,
            "raw_sha256": hashlib.sha256(data).hexdigest(), "start": start, "stop_record_present": stopped,
            "partial_tail_lines": damaged, "invalid_sample_lines": invalid, "gaps": gaps, "samples": samples,
            "note": "Raw audit only; phases, business policy, restoration and actual observations remain separate."}


def collect(device: object, out: Path, plan: dict) -> list[Path]:
    """One read-only pull. Hash-named immutable snapshots never overwrite older raw."""
    with device.ftp() as ftp:
        names = ftp.nlst(REMOTE)
    run_id = plan["config"]["run_id"]
    usage.require(re.fullmatch(r"[0-9a-f]{32}", run_id) is not None, "invalid planned run_id")
    snapshots = []
    for name in names:
        filename = name.rsplit("/", 1)[-1]
        if not re.fullmatch(r"usage-evidence-" + run_id + r"-[0-9a-f]{16}\.jsonl", filename):
            continue
        data = device.read(REMOTE + "/" + filename)
        digest = hashlib.sha256(data).hexdigest()
        path = out / "raw" / (filename[:-6] + "-" + digest + ".jsonl")
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            usage.require(path.read_bytes() == data, "snapshot path hash collision")
        else:
            with path.open("xb") as stream:
                stream.write(data)
        # Raw bytes are preserved even if a subsequent audit fails.
        result = audit(data, plan["config"])
        result["remote_path"] = REMOTE + "/" + filename
        audit_path = path.with_suffix(".audit.json")
        if not audit_path.exists():
            save_new(audit_path, result)
        snapshots.append(path)
    return snapshots


def stage(device: object, out: Path, plan: dict) -> dict:
    """Copy an inactive candidate; never replace live files or arm capture."""
    config = plan["config"]
    run_id = config["run_id"]
    usage.require(re.fullmatch(r"[0-9a-f]{32}", run_id) is not None, "invalid staging run_id")
    packages = Path(plan["packages"])
    scope = plan.get("identity_scope")
    if scope == IDENTITY_SCOPE:
        manifest, hashes, components = standard_candidate_identity(packages)
    else:
        # Old plans retain their full package binding; never silently narrow it.
        usage.require(scope is None, "unknown candidate identity scope")
        manifest, _, hashes, components = qualification.package_identity(packages)
    usage.require(hashes == plan["zip_sha256"] and components == plan["release_components"] and
                  all(manifest[key] == config[key] for key in ("commit", "release_id")),
                  "prepared candidate changed")
    journal_path = out / "stage-install.json"
    usage.require(not journal_path.exists(), "existing staging journal; inspect instead of replaying")
    remote = STAGING + "/" + run_id
    with device.ftp() as ftp:
        try:
            ftp.nlst(remote)
        except (ftplib.error_perm, ftplib.error_temp) as exc:
            # ftpd reports missing directories as 450; ambiguous FTP errors stop.
            usage.require(str(exc).startswith(("450", "550")) and
                          "no such file or directory" in str(exc).lower(), "cannot prove staging directory absent")
        else:
            raise usage.UsageQualificationError("remote staging directory exists; never overwrite")
    live_paths = ["/" + member for member in components] + ["/switch/playwise/build.json",
                  "/switch/playwise/rules.json", "/switch/playwise/handover/runtime-ready.json"]
    before = {path: hashlib.sha256(device.read(path)).hexdigest() for path in live_paths}
    with zipfile.ZipFile(packages / qualification.STANDARD_ZIP) as archive:
        files = {member: archive.read(member) for member in components}
        files["switch/playwise/build.json"] = archive.read("switch/playwise/build.json")
    files["switch/playwise/support/usage-evidence.json"] = (out / "usage-evidence.json").read_bytes()
    usage.require(qualification.read_json_text(files["switch/playwise/support/usage-evidence.json"],
                  "staging config") == config, "local capture config changed")
    journal = {"schema_version": 1, "kind": "inactive_candidate_staging", "phase": "prepared",
               "subject": {key: config[key] for key in ("commit", "release_id")}, "run_id": run_id,
               "remote": remote, "live_before_sha256": before, "writes": [], "candidate_loaded": False,
               "capture_armed": False, "qualified": False}
    save_new(journal_path, journal)
    # Summary may advance; the operation trail retains every original intent.
    def event(kind: str, **values: object) -> None:
        with (out / "stage-events.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"event": kind, **values}, ensure_ascii=False) + "\n")
        from usage_probe import dump
        dump(journal_path, journal)
    try:
        for member, data in files.items():
            target = remote + "/" + member
            item = {"path": target, "sha256": hashlib.sha256(data).hexdigest(), "outcome": "intent_no_replay"}
            journal["writes"].append(item)
            event("write_intent", **item)
            device.write(target, data)
            usage.require(device.read(target) == data, "inactive staging readback mismatch")
            item["outcome"] = "verified"
            event("write_verified", path=target, sha256=item["sha256"])
        for member, data in files.items():
            usage.require(device.read(remote + "/" + member) == data, "staging bundle changed")
        after = {path: hashlib.sha256(device.read(path)).hexdigest() for path in live_paths}
        usage.require(before == after, "live files or runtime changed during staging; inspect")
        journal.update(phase="inactive_staging_verified", live_after_sha256=after)
        event("staging_verified_live_unchanged")
    except Exception as exc:
        journal.update(phase="staging_requires_inspection_no_replay", error=str(exc))
        event("staging_exception", error=str(exc))
        raise
    return journal


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare")
    p.add_argument("--packages", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--duration-s", type=int, default=3600)
    p = commands.add_parser("audit")
    p.add_argument("--raw", type=Path, required=True)
    p.add_argument("--plan", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("collect")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--host", required=True)
    p.add_argument("--ftp-port", type=int, default=21)
    p = commands.add_parser("stage")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--host", required=True)
    p.add_argument("--ftp-port", type=int, default=21)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args.packages, args.out, args.duration_s)
        print(f"PREPARED (not deployed/qualified): {args.out.resolve()}")
    elif args.command == "audit":
        plan = usage.read_json(args.plan)
        save_new(args.output, audit(args.raw.read_bytes(), plan["config"]))
        print(f"AUDITED (not qualified): {args.output.resolve()}")
    else:
        from usage_probe import Device
        plan = usage.read_json(args.out / "plan.json")
        device = Device(args.host, args.ftp_port)
        if args.command == "stage":
            result = stage(device, args.out, plan)
            print(f'INACTIVE STAGING VERIFIED: {result["remote"]}; capture unarmed, live identity unchanged')
        else:
            snapshots = collect(device, args.out, plan)
            print(f"READ-ONLY: {len(snapshots)} immutable raw snapshots; no status/control requests")


if __name__ == "__main__":
    try:
        main()
    except (usage.UsageQualificationError, qualification.QualificationError, OSError, ValueError, KeyError) as exc:
        raise SystemExit(f"FAIL: {exc}") from exc
