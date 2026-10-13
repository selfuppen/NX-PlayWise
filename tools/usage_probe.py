#!/usr/bin/env python3
"""Independent hardware usage experiment: raw counters, events and deferred visuals.

This tool never consumes Lab reports or claims release qualification. All writes
target a dedicated, operator-authorized test console. No PINs/credentials are read.
"""
from __future__ import annotations

import argparse
import datetime as dt
import ftplib
import hashlib
import html
import io
import json
from pathlib import Path
import socket
import time
import uuid
import zipfile

import package_usage_probe as pkg
from usage_probe_analysis import focus_for_samples, stats_window, unique_events
from sys_botbase_client import SysBotBaseClient

REMOTE = "/switch/playwise-usage-probe"
RELEASE = "/switch/playwise"
FLAG = "/atmosphere/contents/4200000000BD2300/flags/boot2.flag"
FLAG_BACKUP = FLAG + ".usage-probe"
PROBE_FLAG = "/atmosphere/contents/4200000000BD23F1/flags/boot2.flag"
NS = 1_000_000_000
OVERRIDE = "/atmosphere/config/override_config.ini"


def dump(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp-" + uuid.uuid4().hex)
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for attempt in range(5):
        try:
            temp.replace(path)
            return
        except PermissionError:
            # Windows readers can briefly deny replacement. Keep the old
            # journal and retry atomically; never delete it to force a write.
            if attempt == 4:
                raise
            time.sleep(.1)


class Device:
    def __init__(self, host: str, ftp_port: int = 21, bot_port: int = 6000) -> None:
        self.host, self.ftp_port, self.bot_port = host, ftp_port, bot_port

    def ftp(self) -> ftplib.FTP:
        f = ftplib.FTP()
        f.connect(self.host, self.ftp_port, timeout=8)
        f.login()
        return f

    def read(self, path: str) -> bytes:
        with self.ftp() as f:
            b = bytearray()
            f.retrbinary("RETR " + path, b.extend)
            return bytes(b)

    def json(self, path: str) -> dict:
        return json.loads(self.read(path))

    def write(self, path: str, data: bytes) -> None:
        with self.ftp() as f:
            parent = str(Path(path).parent).replace("\\", "/")
            current = ""
            for part in parent.strip("/").split("/"):
                current += "/" + part
                try:
                    f.mkd(current)
                except ftplib.error_perm as e:
                    # Existing directories are confirmed; other permission errors propagate.
                    try:
                        f.cwd(current)
                    except ftplib.error_perm:
                        raise e
            f.storbinary("STOR " + path + ".tmp", io.BytesIO(data))
            try:
                f.rename(path + ".tmp", path)
            except ftplib.error_perm as exc:
                if not str(exc).startswith("553"):
                    raise
                # ftpd cannot rename over an existing FAT file. Preserve its
                # preimage until the replacement succeeds; never delete first.
                previous = path + ".previous"
                try:
                    f.size(previous)
                except ftplib.error_perm as missing:
                    if not str(missing).startswith("550"):
                        raise
                else:
                    raise RuntimeError("unresolved FTP replacement: " + previous)
                f.rename(path, previous)
                try:
                    f.rename(path + ".tmp", path)
                except Exception:
                    f.rename(previous, path)
                    raise
                f.delete(previous)

    def put_json(self, path: str, value: dict) -> None:
        self.write(path, json.dumps(value, separators=(",", ":")).encode())

    def bot(self) -> SysBotBaseClient:
        return SysBotBaseClient(self.host, self.bot_port, timeout=8)

    def screenshot(self, path: Path) -> None:
        with self.bot() as c:
            c.capture_screenshot(path)

    def buttons(self, sequence: str) -> None:
        with self.bot() as c:
            # Installed upstream 2.5 does not necessarily include local clickSeq.
            held: list[str] = []
            try:
                for token in sequence.split(","):
                    if token.startswith("W"):
                        time.sleep(int(token[1:]) / 1000)
                    elif token.startswith("+"):
                        c.press(token[1:]); held.append(token[1:])
                    elif token.startswith("-"):
                        c.release(token[1:]); held.remove(token[1:])
                    else:
                        c.click(token); time.sleep(0.2)
            finally:
                for button in reversed(held):
                    c.release(button)

    def command(self, op: str, value: int | str | None = None, *, timeout: float = 25) -> dict:
        request_id = uuid.uuid4().hex
        q: dict = {"id": request_id, "op": op}
        if value is not None:
            q["value"] = value
        self.put_json(REMOTE + "/command.json", q)
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            try:
                result = self.json(REMOTE + "/results/" + request_id + ".json")
                if result.get("id") != request_id or result.get("op") != op:
                    raise ValueError("mismatched command response")
                return result
            except (ftplib.error_perm, socket.timeout, OSError, EOFError):
                time.sleep(0.5)
        # Never resend a mutation with a different id after an ambiguous timeout.
        raise TimeoutError(f"probe command {op} outcome unknown: {request_id}")


def checked(result: dict) -> dict:
    if result.get("ok") is not True:
        raise RuntimeError(f"probe refused {result.get('op')}: {result.get('raw_result')}")
    return result


def deploy(d: Device, path: Path) -> dict:
    identity = pkg.verify(path)
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            data = z.read(name)
            d.write("/" + name, data)
            if hashlib.sha256(d.read("/" + name)).digest() != hashlib.sha256(data).digest():
                raise RuntimeError(f"FTP readback mismatch: {name}")
    return identity


def handoff(d: Device, out: Path, *, wait_probe: bool = True) -> dict:
    """Quiesce first; remove only the boot flag after transaction-specific ack."""
    journal = out / "handoff.json"
    if journal.exists():
        raise RuntimeError("existing handoff journal: inspect/cleanup before starting another run")
    identity = d.json(RELEASE + "/handover/runtime-ready.json")
    transaction = uuid.uuid4().hex
    record = {"transaction_id": transaction, "release_identity": identity, "phase": "prepared", "flag_removed": False}
    dump(journal, record)
    d.put_json(RELEASE + "/handover/intent.json", {"version": 1, "transaction_id": transaction, "action": "quiesce", "created_at": int(time.time())})
    until = time.monotonic() + 20
    while time.monotonic() < until:
        try:
            ack = d.json(RELEASE + "/handover/ready.json")
            if ack.get("transaction_id") == transaction and ack.get("status") == "ready" and ack.get("source_pid") == identity.get("pid"):
                break
        except (ftplib.error_perm, OSError):
            pass
        time.sleep(0.5)
    else:
        raise TimeoutError("standard backend did not acknowledge quiesce")
    with d.ftp() as f:
        try:
            f.size(FLAG_BACKUP)
        except ftplib.error_perm as e:
            if not str(e).startswith("550"):
                raise
        else:
            raise RuntimeError("prior boot flag backup exists")
        # Journal the intended rename before executing it, so cleanup covers a lost reply.
        record["phase"] = "flag_rename_pending"
        dump(journal, record)
        f.rename(FLAG, FLAG_BACKUP)
    record.update(phase="waiting_absent", flag_removed=True)
    dump(journal, record)
    if not wait_probe:
        return record
    until = time.monotonic() + 20
    while time.monotonic() < until:
        sample = d.json(REMOTE + "/latest.json")
        if sample.get("exclusive_owner") is True:
            record["phase"] = "exclusive"
            dump(journal, record)
            return record
        time.sleep(1)
    raise TimeoutError("cannot prove exclusive PCTL owner")


def restore_flag(d: Device, out: Path) -> None:
    p = out / "handoff.json"
    if not p.exists():
        return
    record = json.loads(p.read_text(encoding="utf-8"))
    with d.ftp() as f:
        try:
            f.size(FLAG_BACKUP)
        except ftplib.error_perm:
            # Do not invent a boot flag if the original did not exist.
            return
        try:
            f.size(FLAG)
        except ftplib.error_perm:
            f.rename(FLAG_BACKUP, FLAG)
        else:
            raise RuntimeError("boot flag restoration conflict")
    record["phase"] = "flag_restored"
    dump(p, record)


def bootstrap(d: Device, out: Path, *, from_home: bool = False, foreground: bool = False) -> None:
    """Recorded profile of this console: Sphaira, three columns, probe entry 11.

    This is an explicit hardware input profile, not a claim that all hbmenus
    have this layout. A failed identity check preserves screenshots and stops.
    """
    # Operator-confirmed HOME Controllers shortcut; no temporary Album mapping.
    try:
        previous = d.read(REMOTE + "/launcher.json")
    except ftplib.error_perm:
        pass
    else:
        (out / "launcher-before-bootstrap.json").write_bytes(previous)
        with d.ftp() as f:
            f.delete(REMOTE + "/launcher.json")
    if foreground:
        d.write(REMOTE + "/foreground.flag", b"")
    if not from_home:
        d.buttons("HOME")
        time.sleep(1)
    with d.bot() as c:
        c.touch([(730, 548)])
    time.sleep(1)
    d.buttons("+X,W1200,A,W1500,-X")
    time.sleep(4)
    d.screenshot(out / "screenshots/bootstrap-hbmenu-profile.jpg")
    # Separate connections avoid packet coalescing on older sys-botbase builds.
    for key in ("DDOWN", "DDOWN", "DDOWN", "DRIGHT", "A"):
        with d.bot() as c:
            c.click(key)
            time.sleep(0.6)
    time.sleep(3)
    manifest = d.json(REMOTE + "/build.json")
    result = d.json(REMOTE + "/launcher.json")
    if result.get("result") != 0 or result.get("identity") != manifest.get("source_hash"):
        d.screenshot(out / "screenshots/bootstrap-unavailable.jpg")
        raise RuntimeError("launcher identity or process start not confirmed")
    dump(out / "bootstrap-result.json", result)


def restore_bootstrap(d: Device, out: Path) -> None:
    metadata = out / "bootstrap-config.json"
    if not metadata.exists():
        return
    m = json.loads(metadata.read_text(encoding="utf-8"))
    original = (out / m["original_path"]).read_bytes()
    current = d.read(OVERRIDE)
    if current == original:
        return
    if hashlib.sha256(current).hexdigest() != m["modified_sha256"]:
        raise RuntimeError("bootstrap config changed externally; retain preimage for final review")
    d.write(OVERRIDE, original)
    dump(out / "bootstrap-restoration.json", {"restored": d.read(OVERRIDE) == original})


def counter(sample: dict, key: str) -> int | None:
    result_key = {"remaining_ns": "remaining_result", "spent_ns": "spent_result"}[key]
    if sample.get("sample_error") != 0 or sample.get(result_key) != 0:
        return None
    value = sample.get(key)
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def projection(sample: dict, *, clamp: bool) -> int | None:
    remaining = counter(sample, "remaining_ns")
    if remaining is None or not sample.get("configured_available"):
        return None
    minutes = sample.get("configured_minutes")
    if not isinstance(minutes, int) or not 0 <= minutes <= 1440:
        return None
    return minutes * 60 * NS - (max(remaining, 0) if clamp else remaining)


def rows(path: Path) -> tuple[list[dict], bool]:
    if not path.exists():
        return [], False
    output = []
    damaged = False
    for line in path.read_text(encoding="utf-8").splitlines():
        try:
            row = json.loads(line)
            if isinstance(row, dict):
                output.append(row)
            else:
                damaged = True
        except json.JSONDecodeError:
            damaged = True
    return output, damaged


def compare(before: dict, after: dict) -> dict:
    same_boot = before.get("boot_id") == after.get("boot_id")
    elapsed = (int(after["tick_ns"]) - int(before["tick_ns"])) / NS if same_boot else None
    output: dict = {"same_boot": same_boot, "elapsed_s": elapsed}
    output["settings_stable"] = before.get("settings_hex") == after.get("settings_hex")
    output["before_configured_minutes"] = before.get("configured_minutes") if before.get("configured_available") else None
    output["after_configured_minutes"] = after.get("configured_minutes") if after.get("configured_available") else None
    for key in ("remaining_ns", "spent_ns"):
        a, b = counter(before, key), counter(after, key)
        output[key + "_delta_s"] = (b - a) / NS if a is not None and b is not None else None
    for clamp, label in ((False, "signed_used"), (True, "clamped_used")):
        a, b = projection(before, clamp=clamp), projection(after, clamp=clamp)
        output[label + "_delta_s"] = (b - a) / NS if a is not None and b is not None else None
    return output


class Experiment:
    def __init__(self, d: Device, out: Path) -> None:
        self.d, self.out = d, out
        self.phases: list[dict] = []
        self.actions: list[dict] = []
        self.start = time.monotonic()
        self.mutating = False
        self.initial_clock: dict | None = None
        self.clock_changed = False

    def log(self, message: str) -> None:
        print(f"[{dt.datetime.now():%H:%M:%S} +{int(time.monotonic()-self.start)}s] {message}", flush=True)

    def checkpoint(self) -> None:
        dump(self.out / "run.json", {"advisory": True, "phases": self.phases, "actions": self.actions})

    def action(self, op: str, value: int | str | None = None) -> dict:
        self.log(f"操作 {op} {value if value is not None else ''}")
        result = self.d.command(op, value)
        self.actions.append(result)
        self.checkpoint()
        return checked(result)

    def phase(self, name: str, seconds: int, *, requested_context: str) -> None:
        self.log(f"采集 {name}，{seconds}s")
        phase: dict = {"name": name, "requested_context": requested_context, "observation": "pending_review", "duration_target_s": seconds, "screenshots": []}
        self.phases.append(phase)
        def capture(path: Path) -> None:
            try:
                self.d.screenshot(path)
                phase["screenshots"].append(path.relative_to(self.out).as_posix())
            except (OSError, EOFError, RuntimeError, ValueError) as exc:
                phase.setdefault("screenshot_errors", []).append(str(exc))
        try:
            phase["before"] = checked(self.d.command("sample"))["after"]
            path = self.out / "screenshots" / f"{len(self.phases):02d}-{name}-before.jpg"
            capture(path)
            self.checkpoint()
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                time.sleep(min(10, max(0, deadline-time.monotonic())))
                self.checkpoint()
            phase["after"] = checked(self.d.command("sample"))["after"]
            phase["comparison"] = compare(phase["before"], phase["after"])
            path = self.out / "screenshots" / f"{len(self.phases):02d}-{name}-after.jpg"
            capture(path)
            phase["status"] = "captured"
        except (OSError, EOFError, ftplib.Error, ValueError, RuntimeError) as e:
            phase.update(status="inconclusive", reason=str(e))
        self.checkpoint()

    def skipped(self, name: str, reason: str) -> None:
        self.phases.append({"name": name, "status": "precondition_not_met", "reason": reason, "observation": "not_observed"})
        self.checkpoint()

    def download(self, *, live: bool = False) -> None:
        if live:
            self.action("export")
        try:
            self._download()
        finally:
            if live:
                self.action("collect")

    def _download(self) -> None:
        self.log("下载原始样本与事件")
        raw = self.out / "raw"
        raw.mkdir(parents=True, exist_ok=True)
        for name in ("ready.json", "build.json", "baseline.json", "latest.json", "samples.jsonl", "events.jsonl", "stats.jsonl", "ranges.jsonl", "actions.jsonl", "launcher.json", "applet.jsonl"):
            try:
                (raw / name).write_bytes(self.d.read(REMOTE + "/" + name))
            except ftplib.error_perm:
                pass
        for name in ("environment.json", "build.json"):
            try:
                (raw / ("release-" + name)).write_bytes(self.d.read(RELEASE + "/" + name))
            except ftplib.error_perm:
                pass

    def run(self) -> None:
        ready = self.d.json(REMOTE + "/ready.json")
        manifest = self.d.json(REMOTE + "/build.json")
        if ready.get("source_hash") != manifest.get("source_hash"):
            raise RuntimeError("running probe identity does not match installed package")
        latest = self.d.json(REMOTE + "/latest.json")
        if latest.get("boot_id") != ready.get("boot_id"):
            raise RuntimeError("probe boot identity mismatch")
        # Freshness comes from two advancing device ticks, not console wall clock.
        time.sleep(2)
        if int(self.d.json(REMOTE + "/latest.json")["tick_ns"]) <= int(latest["tick_ns"]):
            raise RuntimeError("probe is not sampling")
        self.initial_clock = latest
        dump(self.out / "identity.json", {"probe": ready, "manifest": manifest})
        self.phase("baseline-isolated" if latest.get("exclusive_owner") else "baseline-standard", 120,
                   requested_context="HOME; standard backend already quiesced" if latest.get("exclusive_owner") else "HOME with standard backend")
        try:
            journal = self.out / "handoff.json"
            if journal.exists():
                record = json.loads(journal.read_text(encoding="utf-8"))
                if record.get("phase") != "waiting_absent" or not self.d.json(REMOTE + "/latest.json").get("exclusive_owner"):
                    raise RuntimeError("existing handoff is not a verified bootstrap handoff")
                record["phase"] = "exclusive"
                dump(journal, record)
            else:
                handoff(self.d, self.out)
            self.action("begin")
            self.mutating = True
            self.action("unconfigured")
            self.phase("no-allowance-home", 300, requested_context="HOME; parental controls enabled; daily slot empty")
            self.action("limit", 1)
            self.phase("overrun-limit-1", 12, requested_context="1m allowance after >=5m awake; possible restriction")
            self.action("limit", 3)
            self.phase("overrun-grant-2-a", 12, requested_context="first +2m")
            self.action("limit", 5)
            self.phase("overrun-grant-2-b", 12, requested_context="second +2m")
            self.action("limit", 125)
            self.phase("grant-120-a", 90, requested_context="first +120m")
            self.action("limit", 245)
            self.phase("grant-120-b", 90, requested_context="second +120m")
            self.phase("home-awake-a", 120, requested_context="HOME")
            with self.d.bot() as c:
                c.touch([(910, 548)])
            self.d.buttons("A")
            time.sleep(4)
            self.phase("system-settings", 120, requested_context="system settings requested by touch; verify screenshots")
            self.d.buttons("HOME")
            time.sleep(3)
            self.phase("home-awake-b", 120, requested_context="HOME repeat")
            # A scripted launch is a request, not proof that a game reached foreground.
            with self.d.bot() as c:
                c.touch([(230, 320)])
            self.d.buttons("A,W1500,A,W1500,A")
            time.sleep(12)
            self.phase("application-foreground", 180, requested_context="leftmost installed application; identity + visual review required")
            self.d.buttons("HOME")
            time.sleep(3)
            self.phase("application-background-home", 120, requested_context="HOME with prior application retained")
            self.action("unlimited")
            self.phase("unlimited-home", 120, requested_context="HOME unlimited")
            self.action("limit", 1440)
            self.phase("limit-1440-home", 120, requested_context="HOME limited to 1440m")
            with self.d.bot() as c:
                c.raw("screenOff")
            try:
                self.phase("display-off-awake", 90, requested_context="display/backlight off; not sleep")
            finally:
                with self.d.bot() as c:
                    c.raw("screenOn")
            # Date shifts are deliberately late: they may reset the official day counter.
            reference = self.d.json(REMOTE + "/latest.json")
            clock = int(reference["unix_s"])
            try:
                self.action("clock", str(clock + 120))
                self.clock_changed = True
                self.phase("clock-forward-same-day", 60, requested_context="wall clock +120s")
                self.action("clock", str(clock - 120))
                self.phase("clock-backward-same-day", 60, requested_context="wall clock rollback")
                self.action("clock", str(clock + 86400))
                self.phase("clock-next-day", 60, requested_context="synthetic day change; not natural midnight")
            except RuntimeError as exc:
                self.skipped("clock-mutation", str(exc))
            finally:
                if self.clock_changed:
                    target = int(reference["unix_s"]) + (int(self.d.json(REMOTE + "/latest.json")["steady_s"]) - int(reference["steady_s"]))
                    self.action("clock", str(target))
                    self.clock_changed = False
                    self.phase("clock-original-day", 60, requested_context="return to original local day")
            self.action("stop")
            self.phase("timer-stopped", 90, requested_context="explicit 1452")
            self.action("start")
            self.phase("timer-started", 90, requested_context="explicit 1451; isolated late experiment")
            # Reboot is last among automated mutations: if boot enters a menu,
            # all other evidence is already local and no person is interrupted.
            self.download(live=True)
            self.reboot()
            for name, reason in (
                ("official-control-toggle", "官方 PIN 未知；不尝试猜测或中途询问"),
                ("natural-midnight", "快速批次仅做人工改时跨日，自然跨日未覆盖"),
                ("true-sleep-wake", "未证明断网后的自动唤醒手段；普通灭屏单列，最终人工补测"),
                ("physical-dock-switch", "需要实体插拔底座，留待最终补测"),
            ):
                self.skipped(name, reason)
        finally:
            # Preserve evidence before cleanup and survive partial experiment failures.
            try:
                if self.mutating:
                    if self.clock_changed and self.initial_clock:
                        current = self.d.json(REMOTE + "/latest.json")
                        if current.get("steady_source") != self.initial_clock.get("steady_source"):
                            raise RuntimeError("clock source changed; date restoration requires final attention")
                        target = int(self.initial_clock["unix_s"]) + int(current["steady_s"]) - int(self.initial_clock["steady_s"])
                        self.action("clock", str(target))
                    self.action("finish")
            finally:
                try:
                    with self.d.ftp() as f:
                        try:
                            f.delete(PROBE_FLAG)
                        except ftplib.error_perm:
                            pass
                    restore_flag(self.d, self.out)
                    self.download()
                except (OSError, EOFError, ftplib.Error) as exc:
                    dump(self.out / "cleanup-pending.json", {"reason": str(exc), "needs_final_attention": True})
                self.checkpoint()
                report(self.out)

    def reboot(self) -> None:
        previous = self.d.json(REMOTE + "/ready.json")
        phase: dict = {"name": "system-reboot", "status": "pending", "observation": "pending_review"}
        self.phases.append(phase)
        try:
            self.d.write(PROBE_FLAG, b"")
            self.action("reboot")
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                try:
                    ready = self.d.json(REMOTE + "/ready.json")
                    if ready.get("boot_id") != previous.get("boot_id") and ready.get("source_hash") == previous.get("source_hash"):
                        phase.update(status="captured", before=previous, after=ready)
                        self.action("resume")
                        self.phase("post-reboot-home", 90, requested_context="fresh probe boot after system reboot")
                        return
                except (OSError, EOFError, ftplib.Error):
                    pass
                time.sleep(3)
            phase.update(status="precondition_not_met", reason="120秒内未自动返回；留待最终确认启动画面")
        finally:
            self.checkpoint()

    def replay(self) -> None:
        """Repeat the corrected grant deadline and add NRO/self-process checks."""
        old = json.loads((self.out / "run.json").read_text(encoding="utf-8"))
        self.phases, self.actions = old["phases"], old["actions"]
        ready = self.d.json(REMOTE + "/ready.json")
        manifest = self.d.json(REMOTE + "/build.json")
        if ready.get("source_hash") != manifest.get("source_hash"):
            raise RuntimeError("replay probe does not match installed artifact")
        dump(self.out / "identity-replay.json", {"probe": ready, "manifest": manifest})
        latest = self.d.json(REMOTE + "/latest.json")
        time.sleep(2)
        newer = self.d.json(REMOTE + "/latest.json")
        if (newer.get("boot_id") != ready.get("boot_id")
                or int(newer["tick_ns"]) <= int(latest["tick_ns"])):
            raise RuntimeError("replay probe is not freshly sampling")
        if not newer.get("exclusive_owner"):
            raise RuntimeError("replay requires exclusive owner proof")
        journal = self.out / "handoff.json"
        record = json.loads(journal.read_text(encoding="utf-8"))
        if record.get("phase") == "flag_restored":
            with self.d.ftp() as f:
                f.rename(FLAG, FLAG_BACKUP)
        elif record.get("phase") != "waiting_absent" or not record.get("flag_removed"):
            raise RuntimeError("replay handoff is not ready")
        record.update(phase="exclusive", flag_removed=True)
        dump(journal, record)
        try:
            # Standard autostart is restored after the initial run. The module
            # still proves current process absence before it accepts begin.
            self.action("begin")
            self.mutating = True
            self.action("unconfigured")
            self.phase("repeat-no-allowance", 300, requested_context="corrected probe; HOME no allowance")
            for minutes in (1, 3, 5):
                self.action("limit", minutes)
                self.phase(f"repeat-limit-{minutes}", 12, requested_context=f"total allowance {minutes}m")
            for minutes in (125, 245):
                self.action("limit", minutes)
                self.phase(f"repeat-grant-{minutes}", 90, requested_context="+120m; old small-limit deadline must be cancelled")
            self.download(live=True)
            # Rebooted consoles can already exceed both grant totals. Clear the
            # restriction before unrelated NRO/application capability checks.
            self.action("limit", 1440)
            self.d.write(REMOTE + "/foreground.flag", b"")
            bootstrap(self.d, self.out, from_home=True, foreground=True)
            self.phase("probe-nro-foreground", 90, requested_context="dedicated NRO own-active-time and Applet statistics")
            self.d.buttons("HOME")
            time.sleep(3)
            self.phase("probe-nro-background-home", 90, requested_context="HOME while NoSuspend NRO completes its 180s sampling")
            with self.d.bot() as c:
                c.touch([(910, 548)])
            time.sleep(0.5)
            self.d.buttons("A")
            time.sleep(3)
            self.phase("repeat-system-settings", 120, requested_context="system settings; transition completed before window")
            self.download(live=True)
            self.d.buttons("HOME")
            time.sleep(1)
            with self.d.bot() as c:
                c.touch([(780, 320)])
            self.d.buttons("A,W1500,A,W1500,A")
            time.sleep(8)
            self.phase("application-b-foreground", 120, requested_context="third installed title; independent PDM statistics comparison")
            self.d.buttons("HOME")
            time.sleep(1)
            self.d.buttons("X,W1000,A,W3000")
            self.phase("application-b-exited", 30, requested_context="second application exit and PDM flush")
            reference = self.d.json(REMOTE + "/latest.json")
            if reference.get("local_clock_result") == 0:
                target = str(int(reference["local_unix_s"]) + 86400)
                try:
                    self.action("clock_local", target)
                    self.phase("local-clock-next-day", 60, requested_context="local system clock +1 day; verify user clock readback")
                except RuntimeError as exc:
                    self.skipped("local-clock-mutation", str(exc))
                finally:
                    now = self.d.json(REMOTE + "/latest.json")
                    restore_to = str(int(reference["local_unix_s"]) + int(now["steady_s"]) - int(reference["steady_s"]))
                    # Even a read-only user clock can hide a successful local write.
                    if abs(int(now["local_unix_s"]) - int(restore_to)) > 3:
                        self.action("clock_local", restore_to)
            self.download(live=True)
            self.reboot()
        finally:
            try:
                if self.mutating:
                    self.action("finish")
            finally:
                with self.d.ftp() as f:
                    try:
                        f.delete(PROBE_FLAG)
                    except ftplib.error_perm:
                        pass
                restore_flag(self.d, self.out)
                self.download()
                self.checkpoint()
                report(self.out)


def report(out: Path) -> dict:
    run = json.loads((out / "run.json").read_text(encoding="utf-8"))
    supplemental = out / "supplemental-phases.json"
    if supplemental.exists():
        run["phases"].extend(json.loads(supplemental.read_text(encoding="utf-8"))["phases"])
    annotations_path = out / "phase-annotations.json"
    if annotations_path.exists():
        annotations = json.loads(annotations_path.read_text(encoding="utf-8"))
        for phase in run["phases"]:
            if phase["name"] in annotations:
                note = annotations[phase["name"]]
                phase["annotation"] = note
                if "corrected_before" in note and "after" in phase:
                    t = int(note["corrected_before"]["tick_ns"])
                    if int(phase["before"]["tick_ns"]) <= t < int(phase["after"]["tick_ns"]):
                        phase["original_before"] = phase.get("before")
                        phase["before"] = note["corrected_before"]
                        phase["comparison"] = compare(phase["before"], phase["after"])
                    else:
                        phase["scenario_status"] = "precondition_not_met"
                        phase["annotation"]["reason"] = "设置入口补发发生在窗口结束之后；不能把本窗口判为设置场景"
                if note.get("screenshot") and note["screenshot"] not in phase.get("screenshots", []):
                    phase.setdefault("screenshots", []).append(note["screenshot"])
    observation_path = out / "manual-observations.json"
    observations = json.loads(observation_path.read_text(encoding="utf-8")).get("observations", {}) if observation_path.exists() else {}
    for phase in run["phases"]:
        if phase["name"] in observations:
            phase["manual_observation"] = observations[phase["name"]]
    samples, damaged = rows(out / "raw/samples.jsonl")
    events, event_damage = rows(out / "raw/events.jsonl")
    stats, stats_damage = rows(out / "raw/stats.jsonl")
    identity_path = out / "identity.json"
    if not identity_path.exists():
        identity_path = out / "identity-replay.json"
    if identity_path.exists():
        initial = json.loads(identity_path.read_text(encoding="utf-8"))["probe"]["boot_id"]
        boots = {initial}
        for phase in run["phases"]:
            for endpoint in ("before", "after"):
                boot = phase.get(endpoint, {}).get("boot_id")
                if boot:
                    boots.add(boot)
        samples = [s for s in samples if s.get("boot_id") in boots]
        events = [s for s in events if s.get("boot_id") in boots]
        stats = [s for s in stats if s.get("boot_id") in boots]
    events = unique_events(events)
    for phase in run["phases"]:
        a, b = phase.get("before", {}), phase.get("after", {})
        if a and b and "tick_ns" in a and "tick_ns" in b:
            phase["comparison"] = compare(a, b)
            if not phase["comparison"]["settings_stable"]:
                phase["scenario_status"] = "settings_changed_during_window"
        if phase.get("status") == "captured" and a.get("steady_source") == b.get("steady_source") and "steady_s" in a and "steady_s" in b:
            phase["pdm_focus"] = focus_for_samples(events, a, b)
            phase["pdm_statistics"] = stats_window(stats, int(a["steady_s"]), int(b["steady_s"]))
    summary = {
        "schema_version": 1, "advisory": True,
        "sample_count": len(samples), "event_count": len(events), "stats_count": len(stats),
        "endpoint_capture_count": sum(endpoint in p for p in run["phases"] for endpoint in ("before", "after")),
        "truncated_or_invalid_lines": damaged or event_damage or stats_damage,
        "negative_remaining_count": sum(counter(s, "remaining_ns") is not None and counter(s, "remaining_ns") < 0 for s in samples),
        "spent_success_count": sum(counter(s, "spent_ns") is not None for s in samples),
        "pdm_statistics_success_count": sum(s.get("result") == 0 for s in stats),
        "captured_phases": sum(p.get("status") == "captured" for p in run["phases"]),
        "manual_review_pending": any("manual_observation" not in p for p in run["phases"]), "phases": run["phases"],
    }
    dump(out / "summary.json", summary)
    findings = out / "findings.md"
    lines = ["# 独立使用时间能力探测", "", "本报告为本轮原始采集，不引用旧 Lab 结论，不构成发布资格。", "", f"已取回连续样本 {len(samples)}；PDM 事件 {len(events)}；统计样本 {len(stats)}；本机保存阶段端点 {summary['endpoint_capture_count']}。端点与连续设备日志分别计数，下载失败不表示端点丢失。", f"原始剩余负值 {summary['negative_remaining_count']} 个；1952 成功 {summary['spent_success_count']} 个；PDM 统计成功 {summary['pdm_statistics_success_count']} 个。", "", "## 阶段机器对照", "", "|阶段|状态|耗时秒|剩余变化秒|1952变化秒|有符号推算变化秒|截零推算变化秒|", "|---|---|---:|---:|---:|---:|---:|"]
    for p in run["phases"]:
        c = p.get("comparison", {})
        values = [p["name"], p.get("status", "unknown")] + [str(c.get(k, "未知")) for k in ("elapsed_s", "remaining_ns_delta_s", "spent_ns_delta_s", "signed_used_delta_s", "clamped_used_delta_s")]
        lines.append("|" + "|".join(values) + "|")
    lines += ["", "## 额度操作前后", "", "|操作|成功|原始剩余前秒|原始剩余后秒|1952前秒|1952后秒|", "|---|---|---:|---:|---:|---:|"]
    for a in run["actions"]:
        before, after = a.get("before", {}), a.get("after", {})
        def seconds(s: dict, key: str) -> str:
            v = counter(s, key)
            return "未知" if v is None else str(v / NS)
        lines.append("|" + "|".join([a["op"], str(a.get("ok")), seconds(before, "remaining_ns"), seconds(after, "remaining_ns"), seconds(before, "spent_ns"), seconds(after, "spent_ns")]) + "|")
    lines += ["", "## PDM 焦点事件重建（候选口径）", "", "|阶段|焦点区间并集秒|尚未闭合尾段秒|", "|---|---:|---:|"]
    for phase in run["phases"]:
        focus = phase.get("pdm_focus", {})
        lines.append(f"|{phase['name']}|{focus.get('candidate_focus_union_s', '未知')}|{focus.get('open_tail_s', '未知')}|")
    lines += ["", "当前稳定窗口按 User 时间轴映射；PDM 原始 steady 与系统采样存在 epoch 偏移，不直接相减。累计应用值不是当天用量；历史改时、完整覆盖、日志策略及开放尾段仍需验证。详情见 summary.json 的 pdm_statistics 与 pdm_focus。", "", "## 待最终确认", "", "打开 index.html 核对尚未确认的阶段画面。人工已确认的画面类别来自 manual-observations.json。", "截图不能单独证明软件暂停，声音或运行效果未观察时保持未知。", "睡眠、未知 PIN、实体底座及自然跨日按未覆盖记录，不计作通过。", ""]
    (out / "report.md").write_text("\n".join(lines), encoding="utf-8")
    cards = []
    for p in run["phases"]:
        images = "".join(f'<a href="{html.escape(s)}"><img loading="lazy" src="{html.escape(s)}"></a>' for s in p.get("screenshots", []))
        cards.append(f'<section><h2>{html.escape(p["name"])}</h2><p>{html.escape(p.get("requested_context", p.get("reason", "")))}</p><p>{html.escape(p.get("status", "unknown"))} · {html.escape(p.get("manual_observation", "人工观察尚未确认"))}</p>{images}<pre>{html.escape(json.dumps(p.get("comparison", {}), ensure_ascii=False, indent=2))}</pre></section>')
    traces = [("1952", "#245bc4", lambda s: counter(s, "spent_ns")),
              ("1454有符号推算", "#16734e", lambda s: projection(s, clamp=False)),
              ("现有截零推算", "#b92f46", lambda s: projection(s, clamp=True))]
    all_values = [v / NS for s in samples for _, _, get in traces if (v := get(s)) is not None]
    chart = ""
    if all_values:
        low, high = min(all_values), max(all_values)
        paths = []
        for name, color, get in traces:
            segments: list[list[str]] = [[]]
            previous = None
            for i, s in enumerate(samples):
                value = get(s)
                if value is None or (previous and s.get("boot_id") != previous.get("boot_id")):
                    if segments[-1]:
                        segments.append([])
                if value is not None:
                    segments[-1].append(f"{40+i/max(1,len(samples)-1)*920:.1f},{260-(value/NS-low)/max(1,high-low)*220:.1f}")
                previous = s
            paths.extend(f'<polyline aria-label="{name}" fill="none" stroke="{color}" stroke-width="2" points="{" ".join(points)}"/>' for points in segments if points)
        legend = " · ".join(f'<span style="color:{color}">{name}</span>' for name, color, _ in traces)
        chart = f'<p>{legend}</p><svg viewBox="0 0 1000 300"><text x="40" y="20">累计秒对比（采样序号；缺失和重启断线）</text>{"".join(paths)}</svg>'
    priorities = []
    for p in run["phases"]:
        if p["name"] in {"overrun-limit-1", "overrun-grant-2-a", "grant-120-b", "application-foreground", "application-background-home", "application-exit-check", "repeat-system-settings", "application-b-foreground", "application-b-exited"} and p.get("screenshots"):
            image_path = p["screenshots"][-1]
            priorities.append(f'<section><h2>{html.escape(p["name"])}</h2><a href="{html.escape(image_path)}"><img src="{html.escape(image_path)}"></a><p>{html.escape(p.get("manual_observation", "待人工确认：限制提示、应用界面或退出状态。"))}</p></section>')
    finding_link = '<a href="findings.md">能力结论与未覆盖项</a> · ' if findings.exists() else ""
    supplements = "".join(f'<p><a href="{html.escape(p.relative_to(out).as_posix())}">重启后补测资料：{html.escape(p.parent.name)}</a></p>' for p in sorted(out.glob("supplement*/index.html")))
    page = '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>使用时间探测资料</title><style>body{font:16px system-ui;margin:24px;background:#f0f3fa;color:#172640}section{background:white;padding:20px;margin:18px 0;border-radius:12px}img{width:48%;margin:1%}pre{white-space:pre-wrap}svg{background:white;width:100%}@media(prefers-color-scheme:dark){body{background:#0b0f19;color:#f3f6fd}section,svg{background:#162032}}</style><h1>使用时间探测 · 最终统一查看</h1><p>' + finding_link + '<a href="report.md">机器数据表</a>。画面未作自动判读。请核对进入场景、官方限制与应用运行表现。</p><h2>本批画面观察记录</h2>' + "".join(priorities) + '<details><summary>完整阶段、原始对照与曲线</summary>' + chart + "".join(cards) + "</details></html>"
    (out / "index.html").write_text(page.replace("</html>", supplements + "</html>"), encoding="utf-8")
    hashes = {p.relative_to(out).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob("*")) if p.is_file() and p.name != "sha256.json"}
    dump(out / "sha256.json", hashes)
    return summary


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["deploy", "bootstrap", "run", "replay", "pull", "report", "cleanup"])
    p.add_argument("--host", default="192.168.0.106")
    p.add_argument("--ftp-port", type=int, default=21)
    p.add_argument("--bot-port", type=int, default=6000)
    p.add_argument("--package", type=Path, default=pkg.ZIP)
    p.add_argument("--from-home", action="store_true", help="Sphaira bootstrap starts from an already confirmed HOME page.")
    p.add_argument("--out", type=Path, default=Path("device-reports/usage-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S")))
    args = p.parse_args()
    d = Device(args.host, args.ftp_port, args.bot_port)
    args.out.mkdir(parents=True, exist_ok=True)
    if args.action == "deploy":
        dump(args.out / "deployed.json", deploy(d, args.package))
        d.screenshot(args.out / "screenshots/deployed.jpg")
        print("Deployed with byte-for-byte FTP readback; open Usage Probe launcher through hbmenu.")
    elif args.action == "bootstrap":
        bootstrap(d, args.out, from_home=args.from_home)
    elif args.action == "report":
        report(args.out)
    elif args.action == "cleanup":
        e = Experiment(d, args.out)
        try:
            checked(d.command("finish"))
        finally:
            restore_flag(d, args.out)
            restore_bootstrap(d, args.out)
            e.download()
    elif args.action == "pull":
        Experiment(d, args.out).download()
    else:
        e = Experiment(d, args.out)
        try:
            e.replay() if args.action == "replay" else e.run()
        except (OSError, EOFError, ftplib.Error, ValueError, RuntimeError) as exc:
            chain = []
            current: BaseException | None = exc
            while current is not None:
                chain.append({"error": str(current), "type": type(current).__name__})
                current = current.__context__
            dump(args.out / "error.json", {"error": str(exc), "type": type(exc).__name__, "chain": chain})
            e.checkpoint()
            try:
                e.download()
            except (OSError, EOFError, ftplib.Error) as download_error:
                dump(args.out / "download-pending.json", {"error": str(download_error), "type": type(download_error).__name__})
            finally:
                report(args.out)
            raise


if __name__ == "__main__":
    main()
