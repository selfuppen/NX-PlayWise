from __future__ import annotations

from contextlib import contextmanager
import ftplib
import json
from pathlib import Path
import sys
import tempfile
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import auto_update as update


def require(value: object, label: str) -> None:
    if not value:
        raise AssertionError(label)


class Clock:
    value = 0.0

    def __call__(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class Console:
    def __init__(self):
        self.files = {"/" + name: b"old-component" for name in update.COMPONENTS}
        self.files[update.FLAG] = b""
        self.files[update.APP + "/build.json"] = json.dumps({"release_id": "old"}).encode()
        self.files[update.APP + "/package-artifacts.json"] = json.dumps({"release_id": "old", "artifacts": {
            name: {"sha256": update.digest(b"old-component"), "size": len(b"old-component")}
            for name in update.COMPONENTS}}).encode()
        self.files[update.RUNTIME] = json.dumps({"profile": "release", "title_id": "4200000000BD2300",
            "ipc_version": 2, "pid": 1, "boot_id": "a" * 16, "release_id": "old"}).encode()
        for name in ["rules.json", "state.json", "compatibility.json"]:
            self.files[update.APP + "/" + name] = b"{}"
        self.setup("active")
        self.directories = {"/atmosphere/fatal_reports", update.APP + "/inbox/pending",
                            update.APP + "/inbox/processing", update.APP + "/results"}
        self.foreground = "0123456789ABCDEF"
        self.actions = []
        self.writes = []
        self.requests = []
        self.no_audit = self.fail_readback = self.fail_sensitive = self.screenshot_fails = False
        self.reconfirm_presses = 0
        self.running = "1"

    def setup(self, phase):
        self.files[update.APP + "/setup.json"] = json.dumps({"phase": phase}).encode()

    def read(self, path):
        if path not in self.files:
            raise ftplib.error_perm("550 No such file or directory")
        if self.fail_readback and self.writes and path == "/" + update.COMPONENTS[0]:
            return b"corrupted"
        return self.files[path]

    def json(self, path):
        return json.loads(self.read(path))

    def write(self, path, data):
        self.writes.append(path)
        self.files[path] = data
        if path == update.APP + "/build.json":
            self.setup("protection")
            self.files[update.APP + "/flags/disable.flag"] = b"runtime_fingerprint_changed\n"

    def acknowledge(self, q):
        rid = q["request_id"]
        self.requests.append((rid, q["type"]))
        self.files[update.APP + "/inbox/done/" + rid + ".json"] = json.dumps(q).encode()
        phase = self.json(update.APP + "/setup.json")["phase"]
        r = {"version": 1, "request_id": rid, "type": q["type"], "status": "ok",
             "setup": {"phase": phase, "disable_flag_present": phase != "active", "recovery_active": False,
                       "disable_reason": "runtime_fingerprint_changed" if phase != "active" else ""},
             "state": {"play_timer_enabled": 1, "restricted_now": 0, "unrestricted_today": 1,
                       "clock": {"day_index": 2479, "second_of_day": 30000},
                       "bedtime": {"enabled": False}, "eye_care": {"enabled": False},
                       "dock": {"force_docked": False, "undocked_limit_enabled": False}}}
        self.files[update.APP + "/results/" + rid + ".json"] = json.dumps(r).encode()

    def put_json(self, path, value):
        self.acknowledge(value)

    @contextmanager
    def ftp(self):
        yield self

    def nlst(self, path):
        prefix = path.rstrip("/") + "/"
        children = sorted({name[len(prefix):].split("/", 1)[0] for name in self.files if name.startswith(prefix)})
        if not children and path not in self.directories:
            raise ftplib.error_perm("550 No such file or directory")
        return children

    @contextmanager
    def bot(self):
        yield self

    def get_title_id(self):
        return self.foreground

    def raw(self, command, reply):
        require(command.startswith("isProgramRunning"), "test console only exposes the read-only process query")
        return self.running

    def capture_screenshot(self, path):
        if self.screenshot_fails:
            raise OSError("capture unavailable")
        Path(path).write_bytes(b"jpeg-fixture")

    def activate(self):
        self.files[update.RUNTIME] = json.dumps({"profile": "release", "title_id": "4200000000BD2300",
            "ipc_version": 2, "pid": 2, "boot_id": "b" * 16, "release_id": "new"}).encode()
        self.acknowledge({"version": 1, "request_id": "ui-status", "type": "status", "payload": {}})
        if not self.no_audit:
            txn = "e" * 32
            self.files[update.EVENTS + "/" + txn + ".jsonl"] = (json.dumps({"transaction_id": txn,
                "event": "status_submitted", "target_release_id": "new", "target_pid": 2,
                "source_pid": 1, "source_release_id": "old", "source_boot_id": "a" * 16,
                "target_boot_id": "b" * 16,
                "request_id": "ui-status"}) + "\n").encode()


class Input:
    def __init__(self, bot, out, **kwargs):
        self.bot, self.label, self.private = bot, out.name, False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    @contextmanager
    def sensitive_input(self):
        self.private = True
        try:
            yield
        finally:
            self.private = False

    def hold(self, buttons, **kwargs):
        if self.private:
            if self.bot.fail_sensitive:
                raise RuntimeError("SECRET_SENTINEL")
            return
        self.bot.actions.append((self.label, buttons))
        if self.label == "close-source":
            self.bot.foreground = ""
        elif self.label == "open-target":
            self.bot.foreground = "0123456789ABCDEF"
        elif self.label == "confirm-handover":
            self.bot.activate()
        elif self.label == "reconfirm":
            self.bot.reconfirm_presses += 1
            if self.bot.reconfirm_presses == 2:
                self.bot.setup("active")
                del self.bot.files[update.APP + "/flags/disable.flag"]
                self.bot.acknowledge({"version": 1, "request_id": "reconfirm", "type": "complete_setup", "payload": {}})


def fixture(root):
    manifest = {"profile": "release", "release_id": "new", "hot_reload_events_version": 1,
                "build": {"source_fingerprint": "c" * 64}}
    files = {name: b"new-component" for name in update.COMPONENTS}
    files["switch/playwise/build.json"] = json.dumps(manifest).encode()
    files["switch/playwise/package-artifacts.json"] = b"{}"
    files["atmosphere/contents/4200000000BD2300/flags/boot2.flag"] = b""
    path = root / "package.zip"
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return path


def prepared(root):
    out = root / "run"
    with mock.patch.object(update, "verify_package_zip") as gate:
        update.prepare(fixture(root), out, host="test.invalid", forwarder="0123456789ABCDEF")
        require(gate.call_count == 1, "prepare must run the authoritative package gate")
    return out


def run_case(console, out, clock):
    with mock.patch.object(update, "verify_package_zip"):
        return update.Updater(out, console, clock=clock, sleep=clock.sleep, input_factory=Input).run("13")


def test_success_and_offline_report():
    with tempfile.TemporaryDirectory() as temporary:
        out = prepared(Path(temporary))
        console, clock = Console(), Clock()
        console.screenshot_fails = True
        result = run_case(console, out, clock)
        require(result["phase"] == "ready_for_tests", "capture failure cannot block trustworthy update completion")
        require(console.writes[-1] == update.APP + "/build.json", "publish build identity after every other file")
        require(len(console.writes) == len(update.load(out, "plan.json")["files"]), "install each allowlisted file once")
        require(console.reconfirm_presses == 2, "one dialog open and one confirm, no replay")
        require(len({rid for rid, kind in console.requests}) == len(console.requests), "request IDs are never reused")
        require((out / "before" / update.COMPONENTS[0]).read_bytes() == b"old-component", "actual rollback preimage retained")
        require('"13"' not in (out / "run.json").read_text(), "runtime PIN is absent from run state")
        with mock.patch.object(update, "Device", side_effect=AssertionError("network forbidden")):
            first = update.report(out)
            require(first == update.report(out), "offline report is reproducible")
        (out / "before-rules.json").write_bytes(b"changed")
        try:
            update.report(out)
        except update.UpdateError:
            pass
        else:
            raise AssertionError("changed sealed evidence must fail")


def test_missing_ack_never_replays():
    with tempfile.TemporaryDirectory() as temporary:
        out = prepared(Path(temporary))
        console = Console()
        console.no_audit = True
        try:
            run_case(console, out, Clock())
        except update.UpdateError:
            pass
        else:
            raise AssertionError("missing transaction evidence must stop")
        require(update.load(out, "run.json")["phase"] == "stopped", "unknown outcome retained")
        require(sum(label == "confirm-handover" for label, buttons in console.actions) == 1, "do not replay confirm")
        count = len(console.actions)
        try:
            run_case(console, out, Clock())
        except update.UpdateError:
            pass
        require(len(console.actions) == count, "restarting the same run cannot send input")


def test_preflight_and_partial_failure():
    for failure in ["foreground", "recovery", "readback", "sensitive"]:
        with tempfile.TemporaryDirectory() as temporary:
            out = prepared(Path(temporary))
            console = Console()
            if failure == "foreground":
                console.foreground = "unexpected"
            elif failure == "recovery":
                console.files[update.APP + "/recovery/active/meta.json"] = b"{}"
            elif failure == "readback":
                console.fail_readback = True
            else:
                console.fail_sensitive = True
            try:
                run_case(console, out, Clock())
            except (update.UpdateError, RuntimeError):
                pass
            else:
                raise AssertionError("injected failure must stop: " + failure)
            require(update.load(out, "run.json")["phase"] == "stopped", "partial run has an explicit stop phase")
            if failure in ("foreground", "recovery"):
                require(not console.actions and not console.writes, "failed preflight changes no device state")
            if failure == "sensitive":
                require("SECRET_SENTINEL" not in (out / "run.json").read_text(), "sensitive backend error text is suppressed")


def test_package_boundary():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        path = fixture(root)
        with zipfile.ZipFile(path, "a") as archive:
            archive.writestr("switch/playwise/rules.json", b"{}")
        with mock.patch.object(update, "verify_package_zip"):
            try:
                update.package_files(path)
            except update.UpdateError:
                pass
            else:
                raise AssertionError("mutable root files cannot enter automatic updates")


def main():
    console = Console()
    with mock.patch.object(console, "nlst", side_effect=ftplib.error_temp("450 No such file or directory")):
        require(update.names(console, "/absent", missing=True) == [], "ftpd missing directory is explicit absence")
    with mock.patch.object(console, "nlst", side_effect=ftplib.error_temp("450 resource busy")):
        try:
            update.names(console, "/busy", missing=True)
        except ftplib.error_temp:
            pass
        else:
            raise AssertionError("transient FTP failure must not be treated as absence")
    for reply in ("550 Permission denied", "550 resource busy", "550 Requested action not taken"):
        for method, read in (("nlst", lambda: update.names(console, "/unknown", missing=True)),
                             ("read", lambda: update.optional(console, "/unknown"))):
            with mock.patch.object(console, method, side_effect=ftplib.error_perm(reply)):
                try:
                    read()
                except ftplib.error_perm:
                    pass
                else:
                    raise AssertionError("ambiguous FTP failure must stop instead of proving absence")
    test_success_and_offline_report()
    test_missing_ack_never_replays()
    test_preflight_and_partial_failure()
    test_package_boundary()
    print("PASS bounded auto update, no replay, preimages, PIN privacy and offline evidence")


if __name__ == "__main__":
    main()
