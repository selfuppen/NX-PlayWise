#!/usr/bin/env python3
"""Bounded updates for an authorized test console in the documented child-NRO profile."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import ftplib
import getpass
import hashlib
import html
import json
import os
import re
import shutil
import time
from typing import Iterator
import uuid
import zipfile

from tools.device.bounded_input_batch import BoundedInputBatch
from tools.package_remote import verify_package_zip
from tools.legacy.usage_probe import Device, dump

APP = "/switch/playwise"
CONTENT = "/atmosphere/contents/4200000000BD2300"
FLAG = CONTENT + "/flags/boot2.flag"
RUNTIME = APP + "/handover/runtime-ready.json"
EVENTS = APP + "/handover/events"
CAPTURE_CONFIG = APP + "/support/usage-evidence.json"
CAPTURE_RESTART = APP + "/support/backend-restart.json"
COMPONENTS = ("switch/playwise/pctc.nro", "switch/.overlays/playwise.ovl",
              "atmosphere/contents/4200000000BD2300/exefs.nsp")
ALLOWED = set(COMPONENTS) | {"switch/playwise/build.json", "switch/playwise/package-artifacts.json",
    "switch/playwise/fonts/emoji.ttf", "atmosphere/contents/4200000000BD2300/flags/boot2.flag"} | {
    "switch/playwise/defaults/" + name + ".json" for name in
    ("auth", "compatibility", "config", "rules", "setup", "state")}
PIN_KEYS = {"0": "X", "1": "DUP", "3": "DRIGHT", "5": "DDOWN", "7": "DLEFT", "9": "Y"}
CAPTURE_EVENTS = ("prepared", "capture_request_claimed", "quiesce_ready", "boot_disabled",
                  "source_absent", "boot_restored", "target_launched", "runtime_ready", "status_submitted")


class UpdateError(RuntimeError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(value: object, message: str) -> None:
    if not value:
        raise UpdateError(message)


def capture_event_chain(rows: list[dict], binding: dict, source: dict, target: dict, transaction: str) -> None:
    for row in rows:
        require(row.get("transaction_id") == transaction and row.get("source_pid") == source["pid"] and
                row.get("source_boot_id") == source["boot_id"] and
                row.get("source_release_id") == source["release_id"] == row.get("target_release_id") and
                row.get("purpose") == "usage_evidence" and row.get("capture_request_id") == binding["request_id"] and
                row.get("capture_run_id") == binding["run_id"] and
                row.get("capture_config_sha256") == binding["config_sha256"] and
                row.get("event") not in ("failure", "failed", "ipc_unavailable"), "capture handover trace differs")
    observed = [row.get("event") for row in rows]
    require(all(observed.count(event) == 1 for event in CAPTURE_EVENTS) and
            [observed.index(event) for event in CAPTURE_EVENTS] == sorted(observed.index(event) for event in CAPTURE_EVENTS),
            "capture restart lacks ordered original handover evidence")
    terminal = rows[observed.index("status_submitted")]
    require(terminal.get("target_pid") == target["pid"] != source["pid"] and
            terminal.get("target_boot_id") == target["boot_id"] != source["boot_id"] and
            target["release_id"] == source["release_id"], "capture handover target differs")


def capture_recovery_target(*, source: dict, binding: dict, current: dict,
                            config_bytes: bytes, claimed: bytes, pending: bytes | None,
                            event_snapshots: dict[str, bytes], raw: bytes,
                            accepted: dict, result: dict) -> dict:
    """Rebind an interrupted observer's target from originals; never mutate a device.

    A missing host target-runtime file is not proof of a foreign process. A
    caller must still check health, installed bytes and ownership before cleanup.
    """
    from tools.device.pctl_standard_evidence import audit
    identity(source)
    identity(current)
    config = json.loads(config_bytes)
    require(binding.get("purpose") == "usage_evidence" and
            binding.get("source_boot_id") == source["boot_id"] and
            binding.get("release_id") == source["release_id"] == config.get("release_id") and
            binding.get("run_id") == config.get("run_id") and
            binding.get("config_sha256") == digest(config_bytes), "recovery capture binding differs")
    require(pending is None and json.loads(claimed) == binding, "recovery request is unclaimed or ambiguous")
    require(len(event_snapshots) == 1, "recovery needs one original new handover transaction")
    filename, data = next(iter(event_snapshots.items()))
    require(re.fullmatch(r"[0-9a-f]{32}\.jsonl", filename) is not None and
            data.endswith(b"\n"), "recovery handover name or incomplete tail")
    rows = [json.loads(line) for line in data.splitlines()]
    capture_event_chain(rows, binding, source, current, filename[:-6])
    require(all(type(row.get("result")) is int and row["result"] == 0 for row in rows),
            "recovery handover contains failed Result")
    terminal = next(row for row in rows if row.get("event") == "status_submitted")
    request_id = terminal.get("request_id")
    require(isinstance(request_id, str) and bool(request_id) and
            accepted.get("request_id") == result.get("request_id") == request_id and
            accepted.get("type") == result.get("type") == "status" and result.get("status") == "ok",
            "recovery lacks original matching successful status receipt")
    observed = audit(raw, config)
    require(observed["start"]["boot_id"] == current["boot_id"] and observed["samples"] and
            not observed["gaps"] and not observed["partial_tail_lines"], "recovery capture stream differs")
    return dict(current)


def load(out: Path, name: str) -> dict:
    return json.loads((out / name).read_bytes())


def ftp_missing(exc: ftplib.Error) -> bool:
    # ftpd uses 450 for a missing directory and 550 for a missing file.
    # These codes also report permission/busy failures. Only an explicit
    # missing-path reply proves absence of recovery/temporary files.
    text = str(exc).lower()
    return text.startswith(("450", "550")) and "no such file or directory" in text


def optional(device: Device, path: str) -> bytes | None:
    try:
        return device.read(path)
    except (ftplib.error_perm, ftplib.error_temp) as exc:
        if not ftp_missing(exc):
            raise
        return None


def names(device: Device, path: str, *, missing: bool = False) -> list[str]:
    try:
        with device.ftp() as ftp:
            return sorted(p.rsplit("/", 1)[-1] for p in ftp.nlst(path))
    except (ftplib.error_perm, ftplib.error_temp) as exc:
        if not missing or not ftp_missing(exc):
            raise
        return []


def identity(value: dict) -> None:
    require(value.get("profile") == "release" and value.get("title_id") == "4200000000BD2300"
            and value.get("ipc_version") == 2 and type(value.get("pid")) is int and value["pid"] > 0
            and value.get("ipc_available", True) is True
            and re.fullmatch(r"[0-9a-f]{16}", value.get("boot_id", ""))
            and isinstance(value.get("release_id"), str), "unverified backend identity")


def package_files(package: Path) -> tuple[dict, dict[str, bytes]]:
    with zipfile.ZipFile(package) as archive:
        members = archive.namelist()
        require(len(members) == len(set(members)), "duplicate Zip members")
        manifest = json.loads(archive.read("switch/playwise/build.json"))
        verify_package_zip(package, "playwise", expected_manifest=manifest)
        files = {name: archive.read(name) for name in members if not name.endswith("/")}
    require(set(files) <= ALLOWED, "package would overwrite a mutable or unsupported path")
    require(manifest.get("hot_reload_events_version") == 1, "target needs the new persistent handover audit")
    require(re.fullmatch(r"[0-9a-f]{64}", manifest.get("build", {}).get("source_fingerprint", "")),
            "target lacks source generation identity")
    return manifest, files


def validate_capture_material(manifest: dict, files: dict[str, bytes], capture: dict,
                              config_bytes: bytes, package_sha256: str, duration_s: int) -> None:
    from tools.qualification.verify_device_qualification import read_json_text
    config = read_json_text(config_bytes, "capture config")
    require(type(manifest.get("capture_restart_version")) is int and manifest["capture_restart_version"] == 1 and
            manifest.get("build", {}).get("source_dirty") is False,
            "capture restart requires a clean candidate with capture_restart_version:1")
    require(config == capture.get("config") and set(config) ==
            {"schema_version", "run_id", "commit", "release_id", "duration_s"} and
            type(config["schema_version"]) is int and config["schema_version"] == 1 and
            re.fullmatch(r"[0-9a-f]{32}", config.get("run_id", "")) and
            re.fullmatch(r"[0-9a-f]{40}", config.get("commit", "")) and
            config.get("commit") == manifest.get("commit") and
            config.get("release_id") == manifest["release_id"] and
            type(config.get("duration_s")) is int and
            max(300, duration_s + 60) <= config["duration_s"] <= 7200,
            "capture configuration identity or lease mismatch")
    require(capture.get("kind") == "production_usage_evidence_plan" and capture.get("qualified") is False and
            capture.get("release_components") == {name: digest(files[name]) for name in COMPONENTS} and
            package_sha256 in capture.get("zip_sha256", {}).values(),
            "capture plan does not bind this exact production package and components")


def prepare(package: Path, out: Path, *, host: str, ftp_port: int = 21, bot_port: int = 6000,
            forwarder: str, duration_s: int = 300, capture_plan: Path | None = None) -> dict:
    require(not out.exists(), "run directory exists; inspect it rather than replaying")
    require(re.fullmatch(r"[0-9A-Fa-f]{16}", forwarder), "forwarder must be an explicit installed application ID")
    require(60 <= duration_s <= 600, "run lease must be 60..600 seconds")
    require(1 <= ftp_port <= 65535 and 1 <= bot_port <= 65535 and bool(host), "invalid endpoint")
    manifest, files = package_files(package)
    capture = None
    if capture_plan is not None:
        from tools.qualification.verify_device_qualification import read_json_text
        capture = read_json_text(capture_plan.read_bytes(), "capture plan")
        config_bytes = capture_plan.with_name("usage-evidence.json").read_bytes()
        validate_capture_material(manifest, files, capture, config_bytes, digest(package.read_bytes()), duration_s)
    out.mkdir(parents=True)
    shutil.copyfile(package, out / "candidate.zip")
    plan = {"version": 1, "run_id": uuid.uuid4().hex, "profile": "child-forwarder-v1",
            "host": host, "ftp_port": ftp_port, "bot_port": bot_port,
            "forwarder": forwarder.upper(), "duration_s": duration_s,
            "package_sha256": digest((out / "candidate.zip").read_bytes()),
            "target_release_id": manifest["release_id"],
            "files": {name: {"sha256": digest(data), "size": len(data)} for name, data in files.items()},
            "requires": ["NRO child page in the specified forwarder; no overlay",
                         "exclusive console and request producers for this bounded run",
                         "same verified input mapping; no game or HOME rearrangement during the run"]}
    if capture is not None:
        (out / "capture-config.json").write_bytes(config_bytes)
        dump(out / "capture-plan.json", capture)
        plan.update(operation="capture_restart", capture_config_sha256=digest(config_bytes))
    dump(out / "plan.json", plan)
    return plan


class Updater:
    def __init__(self, out: Path, device: Device | None = None, *, clock=time.monotonic,
                 sleep=time.sleep, input_factory=BoundedInputBatch) -> None:
        self.out, self.plan = out, load(out, "plan.json")
        require(self.plan.get("version") == 1 and self.plan.get("profile") == "child-forwarder-v1", "unknown run plan")
        require(self.plan.get("operation", "update") in ("update", "capture_restart"), "unknown operation")
        self.capture_restart = self.plan.get("operation") == "capture_restart"
        require(re.fullmatch(r"[0-9a-f]{32}", self.plan.get("run_id", "")), "invalid run identifier")
        self.device = device or Device(self.plan["host"], self.plan["ftp_port"], self.plan["bot_port"])
        self.clock, self.sleep, self.input_factory = clock, sleep, input_factory
        self.deadline = clock() + self.plan["duration_s"]
        self.state: dict = {}
        self.baseline: dict = {}
        self.source: dict = {}
        self.bot = None

    def checkpoint(self, phase: str, **fields: object) -> None:
        self.state.update(phase=phase, **fields)
        dump(self.out / "run.json", self.state)
        with (self.out / "events.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"phase": phase, "utc": datetime.now(timezone.utc).isoformat(),
                                     "monotonic": self.clock()}, ensure_ascii=False) + "\n")

    def guard(self) -> None:
        require(self.clock() < self.deadline, "run lease expired; stop without replay")
        require(names(self.device, "/atmosphere/fatal_reports") == self.baseline["fatal"], "new or changed fatal listing")
        require(names(self.device, "/atmosphere/crash_reports", missing=True) == self.baseline["crash"], "new crash listing")

    def wait(self, seconds: float) -> None:
        self.guard()
        require(self.clock() + seconds < self.deadline, "wait exceeds run lease")
        self.sleep(seconds)
        self.guard()

    def foreground(self, expected: str) -> None:
        require(self.bot.get_title_id().upper() == expected.upper(), "unexpected foreground; input stopped")

    @contextmanager
    def controls(self, label: str, *, actions: int = 20) -> Iterator[BoundedInputBatch]:
        self.guard()
        remaining = min(120, int(self.deadline - self.clock()) - 1)
        require(remaining > 0, "insufficient input lease")
        with self.input_factory(self.bot, self.out / label, health=self.guard,
                                duration_s=remaining, max_actions=actions) as batch:
            yield batch

    def picture(self, label: str) -> None:
        """Capture failures cannot block reliable process observations or recovery."""
        try:
            path = self.out / (label + ".jpg")
            self.bot.capture_screenshot(path)
            dump(self.out / (label + "-picture.json"), {"sha256": digest(path.read_bytes()),
                 "path": path.name, "utc": datetime.now(timezone.utc).isoformat(),
                 "phase": self.state.get("phase"), "manual_observation": None})
        except Exception:
            dump(self.out / (label + "-picture.json"), {"capture": "unavailable", "manual_observation": None})

    def capture_preimage(self, files: dict[str, bytes]) -> None:
        self.source = self.device.json(RUNTIME)
        identity(self.source)
        if self.capture_restart:
            require(self.source["release_id"] == self.plan["target_release_id"], "capture restart requires the current candidate")
        else:
            require(self.source["release_id"] != self.plan["target_release_id"], "target already loaded; no UI action sent")
        require(self.device.json(APP + "/build.json")["release_id"] == self.source["release_id"], "disk/runtime mismatch")
        require(self.device.read(FLAG) == b"", "standard boot flag must be the original empty file")
        require(names(self.device, CONTENT + "/flags") == ["boot2.flag"], "unknown boot flag transaction")
        require(set(names(self.device, APP + "/handover")) <= {"runtime-ready.json", "events"}, "existing handover transaction")
        require(optional(self.device, APP + "/recovery/active/meta.json") is None, "active recovery")
        setup = self.device.json(APP + "/setup.json")
        require(setup.get("phase") == "active", "source must be active before update")
        require(optional(self.device, APP + "/flags/disable.flag") is None, "source is disabled")
        for directory in ("pending", "processing"):
            require(not names(self.device, APP + "/inbox/" + directory), "unresolved queued request")
        dump(self.out / "source-runtime.json", self.source)
        self.state["initial_event_names"] = names(self.device, EVENTS, missing=True)
        preimages = {}
        for name in files:
            self.guard()
            for suffix in (".tmp", ".previous"):
                require(optional(self.device, "/" + name + suffix) is None, "unowned FTP temporary file")
            raw = optional(self.device, "/" + name)
            if raw is None:
                preimages[name] = None
            else:
                path = self.out / "before" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(raw)
                preimages[name] = {"sha256": digest(raw), "size": len(raw)}
        for name in ("rules.json", "setup.json", "state.json", "compatibility.json"):
            raw = self.device.read(APP + "/" + name)
            (self.out / ("before-" + name)).write_bytes(raw)
        dump(self.out / "preimages.json", preimages)
        installed = self.device.json(APP + "/package-artifacts.json")
        require(installed.get("release_id") == self.source["release_id"], "source artifact identity mismatch")
        for name in COMPONENTS:
            require(preimages.get(name) == installed.get("artifacts", {}).get(name), "source component digest mismatch")
        if self.capture_restart:
            require(self.device.json(APP + "/build.json").get("capture_restart_version") == 1,
                    "installed candidate does not support capture restart")
            require(all(self.device.read("/" + name) == data for name, data in files.items()),
                    "capture restart never installs or changes candidate files")
            for path in (CAPTURE_CONFIG, CAPTURE_RESTART):
                for suffix in ("", ".tmp", ".previous"):
                    require(optional(self.device, path + suffix) is None, "unowned capture/restart material")
            claimed = APP + "/support/backend-restart-" + self.plan["run_id"] + ".claimed.json"
            require(optional(self.device, claimed) is None, "capture restart request already claimed")
            self.check_stopped_capture()
        status = self.status()
        dump(self.out / "before-status.json", status)
        state = status["state"]
        if self.capture_restart:
            safe_remaining = False
            if state.get("signed_remaining_ns_available") is True:
                value = state.get("signed_remaining_ns")
                if isinstance(value, str) and re.fullmatch(r"-?[0-9]+", value):
                    safe_remaining = int(value) > (self.plan["duration_s"] + 60) * 1_000_000_000
            require(state.get("restricted_now") == 0 and
                    state.get("temporary_unlocked_available") is True and state.get("temporary_unlocked") is False and
                    (state.get("unrestricted_today") == 1 or
                     state.get("limited_today") == 1 and safe_remaining),
                    "capture restart needs a reliable usable window covering its whole lease; no rules changed")
        else:
            require(state.get("restricted_now") == 0 and state.get("unrestricted_today") == 1,
                    "update requires an unrestricted test window; rules will not be changed automatically")
        require(state.get("bedtime", {}).get("enabled") is False and
                state.get("eye_care", {}).get("enabled") is False and
                state.get("dock", {}).get("force_docked") is False and
                state.get("dock", {}).get("undocked_limit_enabled") is False, "other test restrictions must be inactive")
        second = state.get("clock", {}).get("second_of_day")
        require(type(second) is int and 0 <= second < 86400 - self.plan["duration_s"] - 60,
                "update window must finish before local midnight")

    def check_stopped_capture(self) -> None:
        """Refuse an active/unknown old stream; never stop one to pass preflight."""
        import tools.device.pctl_standard_evidence as evidence
        from tools.qualification.verify_device_qualification import read_json_text
        config = json.loads((self.out / "capture-config.json").read_bytes())
        for name in names(self.device, APP + "/support", missing=True):
            require(not re.fullmatch(r"usage-evidence-" + config["run_id"] + r"-[0-9a-f]{16}\.jsonl", name),
                    "capture run already used; prepare a fresh run instead of renewing its lease")
            if not re.fullmatch(r"usage-evidence-[0-9a-f]{32}-" + self.source["boot_id"] + r"\.jsonl", name):
                continue
            raw = self.device.read(APP + "/support/" + name)
            path = self.out / "source-capture" / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(raw)
            start = read_json_text(raw.splitlines()[0], "source capture start")
            config = {key: start[key] for key in ("commit", "release_id", "run_id")}
            require(start["duration_ns"].isdigit() and int(start["duration_ns"]) % 1_000_000_000 == 0,
                    "source capture lease unknown")
            config["duration_s"] = int(start["duration_ns"]) // 1_000_000_000
            audit = evidence.audit(raw, config)
            require(audit["start"]["boot_id"] == self.source["boot_id"] and
                    audit["start"]["release_id"] == self.source["release_id"] and
                    audit["start"]["commit"] == self.device.json(APP + "/build.json")["commit"] and
                    audit["stop_record_present"] and not audit["invalid_sample_lines"] and
                    not audit["partial_tail_lines"] and not audit["gaps"],
                    "source capture active or uncertain; inspect without rearming")

    def arm_capture_restart(self) -> None:
        config = (self.out / "capture-config.json").read_bytes()
        require(digest(config) == self.plan["capture_config_sha256"], "prepared capture bytes changed")
        value = json.loads(config)
        request = {"schema_version": 1, "purpose": "usage_evidence", "request_id": self.plan["run_id"],
                   "source_boot_id": self.source["boot_id"], "release_id": self.source["release_id"],
                   "run_id": value["run_id"], "config_sha256": digest(config)}
        dump(self.out / "capture-restart-request.json", request)
        claimed = APP + "/support/backend-restart-" + request["request_id"] + ".claimed.json"
        require(optional(self.device, claimed) is None, "capture restart request already claimed")
        for path in (CAPTURE_CONFIG, CAPTURE_RESTART):
            require(optional(self.device, path) is None, "capture materials appeared during preflight")
        require(self.device.json(RUNTIME) == self.source, "source changed before capture arm")
        self.checkpoint("capture_config_pending")
        self.device.write(CAPTURE_CONFIG, config)
        require(self.device.read(CAPTURE_CONFIG) == config, "capture configuration readback mismatch")
        self.checkpoint("capture_restart_pending")
        self.device.put_json(CAPTURE_RESTART, request)
        require(self.device.json(CAPTURE_RESTART) == request, "capture restart request readback mismatch")
        self.checkpoint("capture_restart_armed")

    def verify_capture_started(self) -> None:
        import tools.device.pctl_standard_evidence as evidence
        plan = load(self.out, "capture-plan.json")
        until = self.clock() + 15
        while self.clock() < until:
            self.guard()
            for path in evidence.collect(self.device, self.out / "target-capture", plan):
                audit = json.loads(path.with_suffix(".audit.json").read_bytes())
                require(audit["start"]["boot_id"] == load(self.out, "target-runtime.json")["boot_id"] and
                        not audit["stop_record_present"] and
                        not audit["invalid_sample_lines"] and not audit["gaps"], "new capture invalid or stopped")
                if audit["partial_tail_lines"]:
                    continue  # Preserve live byte tails and wait for a complete snapshot, never restart.
                if audit["samples"]:
                    require(not audit["samples"][-1]["raw"]["temporarily_unlocked"], "capture is temporarily unlocked")
                    dump(self.out / "capture-started.json", {"path": path.relative_to(self.out).as_posix(),
                         "sha256": digest(path.read_bytes()), "boot_id": audit["start"]["boot_id"],
                         "run_id": plan["config"]["run_id"], "qualified": False})
                    claimed = APP + "/support/backend-restart-" + self.plan["run_id"] + ".claimed.json"
                    claimed_bytes = self.device.read(claimed)
                    (self.out / "capture-restart-claimed.json").write_bytes(claimed_bytes)
                    require(json.loads(claimed_bytes) == load(self.out, "capture-restart-request.json") and
                            optional(self.device, CAPTURE_RESTART) is None, "restart request consumption unproved")
                    require(self.device.read(CAPTURE_CONFIG) == (self.out / "capture-config.json").read_bytes(),
                            "capture config changed after handover")
                    return
            self.wait(.5)
        raise UpdateError("new capture outcome unknown; never repeat restart")

    def install(self, files: dict[str, bytes]) -> None:
        # Publish build.json last: the old daemon continuously checks that file.
        order = sorted(files, key=lambda name: name == "switch/playwise/build.json")
        for name in order:
            self.guard()
            require(self.device.json(RUNTIME) == self.source, "source changed during file install")
            self.checkpoint("installing", current_file=name)
            self.device.write("/" + name, files[name])
            require(self.device.read("/" + name) == files[name], "file readback mismatch")
            self.state.setdefault("installed", []).append(name)
            self.checkpoint("installing")
        self.checkpoint("installed")

    def receipt(self, request_id: str, kind: str, *, limit: int = 25) -> dict:
        require(re.fullmatch(r"[A-Za-z0-9_-]{1,79}", request_id), "invalid result identifier")
        until = self.clock() + limit
        while self.clock() < until:
            self.guard()
            raw = optional(self.device, APP + "/results/" + request_id + ".json")
            if raw is None:
                self.wait(.5)
                continue
            result = json.loads(raw)
            require((result.get("request_id"), result.get("type")) == (request_id, kind), "result identity mismatch")
            accepted = self.device.read(APP + "/inbox/done/" + request_id + ".json")
            q = json.loads(accepted)
            require((q.get("request_id"), q.get("type")) == (request_id, kind), "accepted request mismatch")
            directory = self.out / "receipts"
            directory.mkdir(exist_ok=True)
            (directory / (request_id + "-request.json")).write_bytes(accepted)
            (directory / (request_id + "-result.json")).write_bytes(raw)
            require(result.get("status") == "ok", "original operation was refused; do not replay")
            return result
        raise UpdateError("original request outcome unknown; never submit a new ID")

    def status(self) -> dict:
        request_id = uuid.uuid4().hex
        q = {"version": 1, "request_id": request_id, "type": "status", "created_at": int(time.time()), "payload": {}}
        directory = self.out / "requests"
        directory.mkdir(exist_ok=True)
        dump(directory / (request_id + ".json"), q)
        self.checkpoint("status_pending", status_request_id=request_id)
        self.device.put_json(APP + "/inbox/pending/" + request_id + ".json", q)
        return self.receipt(request_id, "status")

    def audit(self) -> dict:
        until = self.clock() + 90
        selected = None
        while self.clock() < until:
            self.guard()
            self.foreground(self.plan["forwarder"])
            candidates = set(names(self.device, EVENTS, missing=True)) - set(self.state["initial_event_names"])
            require(len(candidates) <= 1, "multiple handover transactions; stop")
            if candidates:
                filename = next(iter(candidates))
                require(re.fullmatch(r"[0-9a-f]{32}\.jsonl", filename), "unknown handover event name")
                require(selected in (None, filename), "handover transaction changed")
                selected = filename
                raw = self.device.read(EVENTS + "/" + filename)
                directory = self.out / "audit"
                directory.mkdir(exist_ok=True)
                (directory / (filename + "-" + digest(raw) + ".raw")).write_bytes(raw)
                rows = [json.loads(line) for line in raw.splitlines(keepends=True) if line.endswith(b"\n")]
                for row in rows:
                    require(row.get("transaction_id") == filename[:-6] and
                            row.get("target_release_id") == self.plan["target_release_id"] and
                            row.get("source_pid") == self.source["pid"] and
                            row.get("source_release_id") == self.source["release_id"] and
                            row.get("source_boot_id") == self.source["boot_id"], "foreign handover audit")
                    require(row.get("event") not in ("failure", "failed", "ipc_unavailable"), "handover failed; recovery required")
                    if self.capture_restart:
                        binding = load(self.out, "capture-restart-request.json")
                        require(row.get("purpose") == "usage_evidence" and
                                row.get("capture_request_id") == binding["request_id"] and
                                row.get("capture_run_id") == binding["run_id"] and
                                row.get("capture_config_sha256") == binding["config_sha256"],
                                "capture restart event binding differs")
                    if row.get("event") == "status_submitted":
                        runtime = self.device.json(RUNTIME)
                        identity(runtime)
                        require(runtime["pid"] == row["target_pid"] != self.source["pid"] and
                                runtime["boot_id"] == row.get("target_boot_id") and
                                runtime["boot_id"] != self.source["boot_id"] and
                                runtime["release_id"] == self.plan["target_release_id"], "new runtime identity mismatch")
                        if self.capture_restart:
                            capture_event_chain(rows, binding, self.source, runtime, row["transaction_id"])
                        dump(self.out / "target-runtime.json", runtime)
                        self.checkpoint("loaded", transaction_id=row["transaction_id"])
                        self.receipt(row["request_id"], "status")
                        return runtime
            self.wait(.5)
        raise UpdateError("handover outcome unknown; keep journal, do not repeat confirm")

    def reconfirm(self) -> dict:
        state = self.status()
        if state["setup"]["phase"] == "active" and not state["setup"]["disable_flag_present"]:
            return state
        require(state["setup"]["phase"] == "protection" and
                state["setup"]["disable_reason"] == "runtime_fingerprint_changed" and
                not state["setup"]["recovery_active"], "not the expected upgrade reconfirmation state")
        before = set(names(self.device, APP + "/results"))
        self.checkpoint("reconfirm_pending")
        # The documented profile has returned to the parent Support page at card 0.
        with self.controls("reconfirm", actions=2) as batch:
            self.foreground(self.plan["forwarder"])
            batch.hold(["A"], settle_s=1)
            self.picture("reconfirm-dialog")
            batch.hold(["A"], settle_s=1)
        until = self.clock() + 25
        while self.clock() < until:
            self.guard()
            matches = []
            for name in set(names(self.device, APP + "/results")) - before:
                result = self.device.json(APP + "/results/" + name)
                if result.get("type") == "complete_setup":
                    matches.append(result["request_id"])
            require(len(matches) <= 1, "multiple reconfirmation operations")
            if matches:
                self.receipt(matches[0], "complete_setup")
                self.checkpoint("reconfirmed", reconfirm_request_id=matches[0])
                self.wait(6)
                return self.status()
            self.wait(.5)
        raise UpdateError("reconfirmation outcome unknown; do not press confirm again")

    def run(self, pin: str) -> dict:
        require(1 <= len(pin) <= 16 and all(digit in PIN_KEYS for digit in pin), "PIN is outside this verified input mapping")
        require(not (self.out / "run.json").exists(), "run already started; inspect/report, never replay")
        require(digest((self.out / "candidate.zip").read_bytes()) == self.plan["package_sha256"], "frozen candidate changed")
        manifest, files = package_files(self.out / "candidate.zip")
        require(manifest["release_id"] == self.plan["target_release_id"], "plan target mismatch")
        if self.capture_restart:
            validate_capture_material(manifest, files, load(self.out, "capture-plan.json"),
                                      (self.out / "capture-config.json").read_bytes(), self.plan["package_sha256"],
                                      self.plan["duration_s"])
            require(manifest.get("capture_restart_version") == 1 and
                    manifest.get("build", {}).get("source_dirty") is False and
                    digest((self.out / "capture-config.json").read_bytes()) == self.plan["capture_config_sha256"] and
                    json.loads((self.out / "capture-config.json").read_bytes()) == load(self.out, "capture-plan.json")["config"],
                    "frozen capture restart material differs")
        self.baseline = {"fatal": names(self.device, "/atmosphere/fatal_reports"),
                         "crash": names(self.device, "/atmosphere/crash_reports", missing=True)}
        dump(self.out / "health-baseline.json", self.baseline)
        self.checkpoint("started")
        try:
            with self.device.bot() as self.bot:
                self.foreground(self.plan["forwarder"])
                require(self.bot.raw("isProgramRunning 0x4200000000BD2300", True) == "1", "source is absent")
                self.capture_preimage(files)
                self.checkpoint("preflight_passed")
                self.picture("before-exit")
                with self.controls("close-source", actions=1) as batch:
                    batch.hold(["B"], settle_s=2)
                self.foreground("")
                if self.capture_restart:
                    self.arm_capture_restart()
                else:
                    self.install(files)
                with self.controls("open-target", actions=1) as batch:
                    batch.hold(["A"], settle_s=10)
                self.foreground(self.plan["forwarder"])
                with self.controls("authorize", actions=24) as batch:
                    # The child page opens PIN on a standalone Minus release.
                    # L+R uses a separate two-second hold gate; a short chord
                    # leaves subsequent private digits on the ordinary page.
                    batch.hold(["MINUS"], settle_s=1)
                    with batch.sensitive_input():
                        for digit in pin:
                            batch.hold([PIN_KEYS[digit]])
                        batch.hold(["PLUS"], settle_s=2)
                del pin
                self.picture("authorized-prompt")
                require(self.device.json(RUNTIME) == self.source, "source changed before handover")
                self.checkpoint("handover_confirm_pending")
                with self.controls("confirm-handover", actions=1) as batch:
                    batch.hold(["A"], settle_s=1)
                self.audit()
                if self.capture_restart:
                    self.verify_capture_started()
                state = self.reconfirm()
                require(state["setup"]["phase"] == "active" and not state["setup"]["disable_flag_present"]
                        and not state["setup"]["recovery_active"], "target is loaded but not ready for tests")
                final_rules = self.device.read(APP + "/rules.json")
                require(final_rules == (self.out / "before-rules.json").read_bytes(), "rules changed")
                require(state["state"]["clock"]["day_index"] ==
                        load(self.out, "before-status.json")["state"]["clock"]["day_index"], "device day changed during update")
                dump(self.out / "final-status.json", state)
                (self.out / "final-rules.json").write_bytes(final_rules)
                require(self.device.read(FLAG) == b"", "boot flag not restored")
                require(set(names(self.device, APP + "/handover")) <= {"runtime-ready.json", "events"}, "handover residue")
                require(not names(self.device, APP + "/inbox/pending") and
                        not names(self.device, APP + "/inbox/processing"), "queue has unresolved requests")
                hashes = {}
                for name, data in files.items():
                    self.guard()
                    observed = self.device.read("/" + name)
                    require(observed == data, "installed candidate changed")
                    hashes[name] = {"sha256": digest(observed), "size": len(observed)}
                dump(self.out / "final-installed.json", hashes)
                self.picture("target-active")
                with self.controls("return-child", actions=1) as batch:
                    batch.hold(["B"], settle_s=1)
                self.foreground(self.plan["forwarder"])
                self.picture("child-ready")
                until = self.clock() + 5
                while names(self.device, APP + "/inbox/pending") or names(self.device, APP + "/inbox/processing"):
                    require(self.clock() < until, "child refresh queue is unresolved")
                    self.wait(.5)
                self.checkpoint("ready_for_tests", qualified=False)
                return self.state
        except Exception as exc:
            # A mutation or input may have reached the console. Persist its phase,
            # never retry it or guess a rollback from an exception string.
            previous = self.state.get("phase")
            self.checkpoint("stopped", stopped_at=previous, failure_type=type(exc).__name__,
                            reason=str(exc) if isinstance(exc, UpdateError) else None,
                            recovery="inspect_original_transaction_no_replay", qualified=False)
            raise


def report(out: Path) -> dict:
    """Offline only; never reconnect or change sealed evidence."""
    plan, state = load(out, "plan.json"), load(out, "run.json")
    require(digest((out / "candidate.zip").read_bytes()) == plan["package_sha256"], "candidate changed")
    if state["phase"] == "ready_for_tests":
        source, target = load(out, "source-runtime.json"), load(out, "target-runtime.json")
        identity(source)
        identity(target)
        require(target["release_id"] == plan["target_release_id"] and target["pid"] != source["pid"]
                and target["boot_id"] != source["boot_id"], "offline runtime mismatch")
        require(load(out, "final-installed.json") == plan["files"], "offline installed file mismatch")
        require((out / "final-rules.json").read_bytes() == (out / "before-rules.json").read_bytes(), "offline rules mismatch")
        if plan.get("operation") == "capture_restart":
            import tools.device.pctl_standard_evidence as evidence
            capture = load(out, "capture-started.json")
            raw = (out / capture["path"]).read_bytes()
            audit = evidence.audit(raw, load(out, "capture-plan.json")["config"])
            require(digest(raw) == capture["sha256"] and audit["start"]["boot_id"] == target["boot_id"] and
                    audit["samples"] and not audit["stop_record_present"] and not audit["invalid_sample_lines"] and
                    not audit["partial_tail_lines"] and not audit["gaps"], "offline capture start unproved")
            require(source["release_id"] == target["release_id"] and
                    digest((out / "capture-config.json").read_bytes()) == plan["capture_config_sha256"],
                    "offline capture restart identity differs")
            require(load(out, "capture-restart-request.json") == load(out, "capture-restart-claimed.json"),
                    "offline capture request claim differs")
            traces = []
            for path in (out / "audit").glob("*.raw"):
                rows = [json.loads(line) for line in path.read_bytes().splitlines(keepends=True) if line.endswith(b"\n")]
                if any(row.get("event") == "status_submitted" for row in rows):
                    capture_event_chain(rows, load(out, "capture-restart-request.json"), source, target, state["transaction_id"])
                    traces.append(path)
            require(traces, "offline capture handover trace missing")
        final = load(out, "final-status.json")
        require(final["setup"]["phase"] == "active" and not final["setup"]["disable_flag_present"]
                and not final["setup"]["recovery_active"], "offline target is not ready")
        for path in (out / "receipts").glob("*-result.json"):
            result = json.loads(path.read_bytes())
            request = json.loads(path.with_name(path.name.replace("-result.json", "-request.json")).read_bytes())
            require((result["request_id"], result["type"]) == (request["request_id"], request["type"])
                    and result["status"] == "ok", "offline receipt mismatch")
    if not (out / "verification.json").exists():
        labels = {"before-exit": "更新前孩子区", "authorized-prompt": "PIN 后待确认加载",
                  "reconfirm-dialog": "环境变化重新接管确认", "target-active": "目标接管完成",
                  "child-ready": "更新后返回孩子区"}
        cards = []
        for path in sorted(out.glob("*-picture.json")):
            value = json.loads(path.read_bytes())
            label = path.name.removesuffix("-picture.json")
            title = html.escape(labels.get(label, label))
            if value.get("path"):
                require(value["path"] == label + ".jpg", "unknown picture path")
                require(digest((out / value["path"]).read_bytes()) == value["sha256"], "picture digest mismatch")
                cards.append(f'<figure><h2>{title}</h2><p>{html.escape(value.get("utc", ""))} '
                             f'{html.escape(str(value.get("phase", "")))}</p><img src="{html.escape(value["path"])}">'
                             f'<p>SHA-256: {value["sha256"]} · 人工观察待独立填写</p></figure>')
            else:
                cards.append(f'<figure><h2>{title}</h2><p>截图不可用；不据此判断画面。</p></figure>')
        (out / "gallery.html").write_text('<!doctype html><meta charset="utf-8"><title>自动更新集中画面复核</title>'
            '<style>body{font:16px system-ui;background:#eef3fb;color:#172640;margin:24px}'
            'figure{background:white;padding:16px}img{max-width:100%}p{overflow-wrap:anywhere}</style>'
            '<h1>自动更新集中画面复核</h1><p>标题表示采集阶段，真实画面结论另行保存；不赋予候选资格。</p>' + ''.join(cards), encoding="utf-8")
    files = {p.relative_to(out).as_posix(): {"sha256": digest(p.read_bytes()), "size": p.stat().st_size}
             for p in sorted(out.rglob("*")) if p.is_file() and p.name not in ("verification.json",)}
    result = {"run_id": plan["run_id"], "phase": state["phase"], "files": files,
              "ready_for_tests": state["phase"] == "ready_for_tests", "qualified": False,
              "manual_observations": None}
    previous = out / "verification.json"
    if previous.exists():
        require(json.loads(previous.read_bytes()) == result, "previously indexed evidence changed")
    else:
        dump(previous, result)
    return result


def diagnose(out: Path, diagnostics_out: Path) -> dict:
    """Describe sealed handover observations offline, without repairing their verdict."""
    out, diagnostics_out = out.resolve(), diagnostics_out.resolve()
    require(out != diagnostics_out and out not in diagnostics_out.parents and
            diagnostics_out not in out.parents, "diagnostics must be outside the original report")
    seal_path = out / "verification.json"
    seal = load(out, "verification.json")
    files = {p.relative_to(out).as_posix(): p for p in out.rglob("*")
             if p.is_file() and p != seal_path}
    require(set(files) == set(seal["files"]), "sealed source file set changed")
    for name, entry in seal["files"].items():
        path = files[name]
        require(out in path.resolve().parents and path.stat().st_size == entry["size"] and
                digest(path.read_bytes()) == entry["sha256"], "sealed source evidence changed")
    plan, state, source = load(out, "plan.json"), load(out, "run.json"), load(out, "source-runtime.json")
    capture = plan.get("operation") == "capture_restart"
    binding = load(out, "capture-restart-request.json") if capture else None
    required = CAPTURE_EVENTS if capture else ("status_submitted",)
    snapshots = []
    for path in sorted((out / "audit").glob("*.raw")):
        match = re.fullmatch(r"([0-9a-f]{32})\.jsonl-([0-9a-f]{64})\.raw", path.name)
        require(match is not None and digest(path.read_bytes()) == match[2], "audit snapshot name/hash differs")
        raw = path.read_bytes()
        rows, malformed = [], []
        for line_number, line in enumerate(raw.splitlines(keepends=True), 1):
            if not line.endswith(b"\n"):
                continue
            try:
                row = json.loads(line)
                require(isinstance(row, dict), "event row is not an object")
            except (ValueError, UpdateError):
                malformed.append(line_number)
            else:
                rows.append(row)
        observed = [row.get("event") for row in rows]
        missing = [event for event in required if event not in observed]
        duplicates = [event for event in required if observed.count(event) > 1]
        positions = [observed.index(event) for event in required if event in observed]
        bindings_match = all(
            row.get("transaction_id") == match[1] and
            row.get("source_pid") == source["pid"] and
            row.get("source_boot_id") == source["boot_id"] and
            row.get("source_release_id") == source["release_id"] and
            row.get("target_release_id") == plan["target_release_id"] and
            (not capture or (row.get("purpose") == "usage_evidence" and
                row.get("capture_request_id") == binding["request_id"] and
                row.get("capture_run_id") == binding["run_id"] and
                row.get("capture_config_sha256") == binding["config_sha256"]))
            for row in rows)
        snapshots.append({
            "path": path.relative_to(out).as_posix(), "sha256": digest(raw), "size": len(raw),
            "transaction_id": match[1], "complete_event_rows": len(rows),
            "observed_events": observed, "missing_required_events": missing,
            "duplicate_required_events": duplicates, "required_order_matches": positions == sorted(positions),
            "bindings_match": bindings_match if rows else None,
            "malformed_complete_lines": malformed,
            "partial_tail_bytes": len(raw.rsplit(b"\n", 1)[-1]) if raw and not raw.endswith(b"\n") else 0,
            "failure_events": [event for event in observed if event in ("failure", "failed", "ipc_unavailable")],
        })
    value = {
        "schema_version": 1, "source_directory": str(out),
        "source_verification_sha256": digest(seal_path.read_bytes()), "source_run_id": plan["run_id"],
        "source_phase": state["phase"], "source_stop_reason": state.get("reason"),
        "operation": plan.get("operation", "update"), "source_runtime": source,
        "required_events": list(required), "snapshots": snapshots,
        "snapshot_order": "filename order; not a chronological assertion",
        "cause": "unknown; missing events do not prove a writer, reader, IPC or lifecycle cause",
        "authorizes_device_operation": False, "qualified": False,
    }
    # Recheck source bytes after analysis. No source file is created or rewritten.
    require(digest(seal_path.read_bytes()) == value["source_verification_sha256"] and
            all(digest(files[name].read_bytes()) == entry["sha256"] for name, entry in seal["files"].items()),
            "source changed during diagnosis")
    encoded = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    output_index = {"schema_version": 1, "qualified": False, "files": {
        "diagnostics.json": {"size": len(encoded), "sha256": digest(encoded)}}}
    if diagnostics_out.exists():
        require(diagnostics_out.is_dir() and
                {p.name for p in diagnostics_out.iterdir()} == {"diagnostics.json", "verification.json"},
                "diagnostics output is not the original two-file result")
        require((diagnostics_out / "diagnostics.json").read_bytes() == encoded and
                load(diagnostics_out, "verification.json") == output_index, "diagnostics output changed")
    else:
        diagnostics_out.mkdir(parents=True)
        with (diagnostics_out / "diagnostics.json").open("xb") as stream:
            stream.write(encoded)
        with (diagnostics_out / "verification.json").open("x", encoding="utf-8") as stream:
            json.dump(output_index, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
    require(load(diagnostics_out, "diagnostics.json") == value and
            load(diagnostics_out, "verification.json") == output_index, "diagnostics JSON round trip differs")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="freeze and verify a package offline")
    prep.add_argument("--package", type=Path, required=True)
    prep.add_argument("--out", type=Path, required=True)
    prep.add_argument("--host", required=True)
    prep.add_argument("--ftp-port", type=int, default=21)
    prep.add_argument("--bot-port", type=int, default=6000)
    prep.add_argument("--forwarder", required=True)
    prep.add_argument("--duration-s", type=int, default=300)
    prep.add_argument("--capture-plan", type=Path, help="explicit same-candidate capture restart; no package installation")
    run = sub.add_parser("run", help="apply one bounded child-NRO update; never replay a started run")
    run.add_argument("--out", type=Path, required=True)
    run.add_argument("--pin-env", help="environment variable name; its value is never saved")
    run.add_argument("--apply", action="store_true")
    check = sub.add_parser("report", help="offline evidence index")
    check.add_argument("--out", type=Path, required=True)
    diagnostic = sub.add_parser("diagnose", help="offline diagnosis into a separate directory; never reconnect or repair")
    diagnostic.add_argument("--out", type=Path, required=True, help="original sealed update report")
    diagnostic.add_argument("--diagnostics-out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            value = prepare(args.package, args.out, host=args.host, ftp_port=args.ftp_port,
                            bot_port=args.bot_port, forwarder=args.forwarder, duration_s=args.duration_s,
                            capture_plan=args.capture_plan)
            print(json.dumps({"prepared": value["run_id"], "target": value["target_release_id"]}))
        elif args.command == "run":
            require(args.apply, "run changes the test console; pass --apply to execute the prepared run")
            pin = os.environ.get(args.pin_env, "") if args.pin_env else getpass.getpass("Authorized test PIN (not recorded): ")
            value = Updater(args.out).run(pin)
            print(json.dumps({"phase": value["phase"], "qualified": False}))
        elif args.command == "diagnose":
            value = diagnose(args.out, args.diagnostics_out)
            print(json.dumps({"source_phase": value["source_phase"], "snapshots": len(value["snapshots"]),
                              "authorizes_device_operation": False, "qualified": False}))
        else:
            value = report(args.out)
            print(json.dumps({"phase": value["phase"], "files": len(value["files"]), "qualified": False}))
        return 0
    except Exception as exc:
        # Error text from a secret-input backend must not be persisted or echoed.
        reason = (": " + str(exc)) if isinstance(exc, UpdateError) else ""
        print("STOP: " + type(exc).__name__ + reason + "; inspect run.json and the original transaction; do not replay")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
