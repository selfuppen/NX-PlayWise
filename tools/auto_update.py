#!/usr/bin/env python3
"""Bounded updates for an authorized test console in the documented child-NRO profile."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import ftplib
import getpass
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import time
from typing import Iterator
import uuid
import zipfile

from bounded_input_batch import BoundedInputBatch
from package_remote import verify_package_zip
from usage_probe import Device, dump

APP = "/switch/playwise"
CONTENT = "/atmosphere/contents/4200000000BD2300"
FLAG = CONTENT + "/flags/boot2.flag"
RUNTIME = APP + "/handover/runtime-ready.json"
EVENTS = APP + "/handover/events"
COMPONENTS = ("switch/playwise/pctc.nro", "switch/.overlays/playwise.ovl",
              "atmosphere/contents/4200000000BD2300/exefs.nsp")
ALLOWED = set(COMPONENTS) | {"switch/playwise/build.json", "switch/playwise/package-artifacts.json",
    "switch/playwise/fonts/emoji.ttf", "atmosphere/contents/4200000000BD2300/flags/boot2.flag"} | {
    "switch/playwise/defaults/" + name + ".json" for name in
    ("auth", "compatibility", "config", "rules", "setup", "state")}
PIN_KEYS = {"0": "X", "1": "DUP", "3": "DRIGHT", "5": "DDOWN", "7": "DLEFT", "9": "Y"}


class UpdateError(RuntimeError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(value: object, message: str) -> None:
    if not value:
        raise UpdateError(message)


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


def prepare(package: Path, out: Path, *, host: str, ftp_port: int = 21, bot_port: int = 6000,
            forwarder: str, duration_s: int = 300) -> dict:
    require(not out.exists(), "run directory exists; inspect it rather than replaying")
    require(re.fullmatch(r"[0-9A-Fa-f]{16}", forwarder), "forwarder must be an explicit installed application ID")
    require(60 <= duration_s <= 600, "run lease must be 60..600 seconds")
    require(1 <= ftp_port <= 65535 and 1 <= bot_port <= 65535 and bool(host), "invalid endpoint")
    manifest, files = package_files(package)
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
    dump(out / "plan.json", plan)
    return plan


class Updater:
    def __init__(self, out: Path, device: Device | None = None, *, clock=time.monotonic,
                 sleep=time.sleep, input_factory=BoundedInputBatch) -> None:
        self.out, self.plan = out, load(out, "plan.json")
        require(self.plan.get("version") == 1 and self.plan.get("profile") == "child-forwarder-v1", "unknown run plan")
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
        status = self.status()
        dump(self.out / "before-status.json", status)
        state = status["state"]
        require(state.get("restricted_now") == 0 and state.get("unrestricted_today") == 1,
                "update requires an unrestricted test window; rules will not be changed automatically")
        require(state.get("bedtime", {}).get("enabled") is False and
                state.get("eye_care", {}).get("enabled") is False and
                state.get("dock", {}).get("force_docked") is False and
                state.get("dock", {}).get("undocked_limit_enabled") is False, "other test restrictions must be inactive")
        second = state.get("clock", {}).get("second_of_day")
        require(type(second) is int and 0 <= second < 86400 - self.plan["duration_s"] - 60,
                "update window must finish before local midnight")

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
                    if row.get("event") == "status_submitted":
                        runtime = self.device.json(RUNTIME)
                        identity(runtime)
                        require(runtime["pid"] == row["target_pid"] != self.source["pid"] and
                                runtime["boot_id"] == row.get("target_boot_id") and
                                runtime["boot_id"] != self.source["boot_id"] and
                                runtime["release_id"] == self.plan["target_release_id"], "new runtime identity mismatch")
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
                self.install(files)
                with self.controls("open-target", actions=1) as batch:
                    batch.hold(["A"], settle_s=10)
                self.foreground(self.plan["forwarder"])
                with self.controls("authorize", actions=24) as batch:
                    batch.hold(["L", "R"], hold_ms=650, settle_s=1)
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
    run = sub.add_parser("run", help="apply one bounded child-NRO update; never replay a started run")
    run.add_argument("--out", type=Path, required=True)
    run.add_argument("--pin-env", help="environment variable name; its value is never saved")
    run.add_argument("--apply", action="store_true")
    check = sub.add_parser("report", help="offline evidence index")
    check.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            value = prepare(args.package, args.out, host=args.host, ftp_port=args.ftp_port,
                            bot_port=args.bot_port, forwarder=args.forwarder, duration_s=args.duration_s)
            print(json.dumps({"prepared": value["run_id"], "target": value["target_release_id"]}))
        elif args.command == "run":
            require(args.apply, "run changes the test console; pass --apply to execute the prepared run")
            pin = os.environ.get(args.pin_env, "") if args.pin_env else getpass.getpass("Authorized test PIN (not recorded): ")
            value = Updater(args.out).run(pin)
            print(json.dumps({"phase": value["phase"], "qualified": False}))
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
