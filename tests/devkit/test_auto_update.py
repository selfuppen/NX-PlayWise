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



def restart_prepared(root):
    path = fixture(root)
    with zipfile.ZipFile(path) as archive:
        files = {name: archive.read(name) for name in archive.namelist()}
    manifest = json.loads(files['switch/playwise/build.json'])
    manifest.update(commit='c' * 40, capture_restart_version=1)
    manifest['build']['source_dirty'] = False
    files['switch/playwise/build.json'] = json.dumps(manifest).encode()
    files['switch/playwise/package-artifacts.json'] = json.dumps({'release_id': 'new', 'artifacts': {
        name: {'size': len(files[name]), 'sha256': update.digest(files[name])} for name in update.COMPONENTS}}).encode()
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in files.items(): archive.writestr(name, data)
    capture = root / 'capture'; capture.mkdir()
    config = {'schema_version': 1, 'run_id': 'f' * 32, 'commit': 'c' * 40, 'release_id': 'new', 'duration_s': 900}
    update.dump(capture / 'plan.json', {'config': config, 'kind': 'production_usage_evidence_plan', 'qualified': False,
                'release_components': {name: update.digest(files[name]) for name in update.COMPONENTS},
                'zip_sha256': {'package.zip': update.digest(path.read_bytes())}})
    update.dump(capture / 'usage-evidence.json', config)
    out = root / 'restart'
    with mock.patch.object(update, 'verify_package_zip'):
        update.prepare(path, out, host='test.invalid', forwarder='0123456789ABCDEF', capture_plan=capture / 'plan.json')
    return out, files


class RestartConsole(Console):
    def __init__(self, files):
        super().__init__()
        self.files.update({'/' + name: data for name, data in files.items()})
        runtime = self.json(update.RUNTIME); runtime['release_id'] = 'new'
        self.files[update.RUNTIME] = json.dumps(runtime).encode()
        self.setup('active')
        self.restart_failure = None

    def read(self, path):
        data = super().read(path)
        if getattr(self, 'restart_failure', None) == 'partial_live' and path.endswith('.jsonl') and '/support/' in path:
            self.restart_failure = None
            return data + b'{'
        return data

    def acknowledge(self, q):
        super().acknowledge(q)
        path = update.APP + '/results/' + q['request_id'] + '.json'
        value = self.json(path)
        value['state'].update(unrestricted_today=0, limited_today=1, temporary_unlocked_available=True,
                              temporary_unlocked=False, signed_remaining_ns_available=True, signed_remaining_ns='900000000000')
        if self.restart_failure == 'restricted': value['state']['restricted_now'] = 1
        if self.restart_failure == 'remaining': value['state']['signed_remaining_ns'] = '360000000000'
        self.files[path] = json.dumps(value).encode()

    def put_json(self, path, value):
        if path == update.CAPTURE_RESTART: self.write(path, json.dumps(value).encode())
        else: super().put_json(path, value)

    def activate(self):
        binding = self.json(update.CAPTURE_RESTART)
        super().activate()
        self.files[update.APP + '/support/backend-restart-' + binding['request_id'] + '.claimed.json'] = self.files.pop(update.CAPTURE_RESTART)
        events = ('prepared', 'capture_request_claimed', 'quiesce_ready', 'boot_disabled', 'source_absent',
                  'boot_restored', 'target_launched', 'runtime_ready', 'status_submitted')
        rows = [{'transaction_id': 'e' * 32, 'event': name, 'source_release_id': 'new', 'target_release_id': 'new',
                 'source_pid': 1, 'target_pid': 2, 'source_boot_id': 'a' * 16, 'target_boot_id': 'b' * 16,
                 'purpose': 'usage_evidence', 'capture_request_id': binding['request_id'], 'capture_run_id': binding['run_id'],
                 'capture_config_sha256': binding['config_sha256'], 'request_id': 'ui-status'} for name in events]
        if self.restart_failure == 'event_binding': rows[2]['capture_run_id'] = '0' * 32
        if self.restart_failure == 'event_order': rows[3], rows[4] = rows[4], rows[3]
        if self.restart_failure == 'no_audit': rows = []
        self.files[update.EVENTS + '/' + 'e' * 32 + '.jsonl'] = ('\n'.join(json.dumps(row) for row in rows) + '\n').encode() if rows else b''
        from test_pctl_standard_evidence import raw_rows, encode
        config = self.json(update.CAPTURE_CONFIG)
        raw = raw_rows()[:2]
        raw[0].update({key: config[key] for key in ('run_id', 'release_id', 'commit')})
        raw[0].update(duration_ns=str(config['duration_s'] * 1_000_000_000), boot_id='b' * 16)
        raw[1].update(run_id=config['run_id'], boot_id='b' * 16)
        if self.restart_failure == 'capture_boot': raw[0]['boot_id'] = raw[1]['boot_id'] = 'a' * 16
        self.files[update.APP + '/support/usage-evidence-' + config['run_id'] + '-' + 'b' * 16 + '.jsonl'] = encode(raw)


def test_capture_restart():
    for live_tail in (False, True):
        with tempfile.TemporaryDirectory() as temporary:
            out, files = restart_prepared(Path(temporary)); console = RestartConsole(files)
            if live_tail: console.restart_failure = 'partial_live'
            result = run_case(console, out, Clock())
            require(result['phase'] == 'ready_for_tests', 'same-candidate restart starts a new bounded capture')
            require(console.writes == [update.CAPTURE_CONFIG, update.CAPTURE_RESTART], 'restart does not reinstall any component')
            require(console.reconfirm_presses == 0, 'same candidate does not fake an environment reconfirmation')
            with mock.patch.object(update, 'Device', side_effect=AssertionError('offline only')):
                first = update.report(out); require(update.report(out) == first, 'restart report reruns offline')
            try: run_case(console, out, Clock())
            except update.UpdateError: pass
            else: raise AssertionError('started restart must never replay')
    for failure in ('restricted', 'remaining', 'event_binding', 'event_order', 'capture_boot', 'no_audit'):
        with tempfile.TemporaryDirectory() as temporary:
            out, files = restart_prepared(Path(temporary)); console = RestartConsole(files)
            console.restart_failure = failure
            try: run_case(console, out, Clock())
            except (update.UpdateError, ValueError): pass
            else: raise AssertionError('restart injection must fail: ' + failure)
            if failure in ('restricted', 'remaining'):
                require(not console.actions and not console.writes, 'failed restart preflight sends no input or config')
            else:
                require(sum(label == 'confirm-handover' for label, buttons in console.actions) == 1,
                        'uncertain handover does not repeat PIN confirmation or restart')
    for failure in ('active_capture', 'unknown_capture_tail', 'claimed', 'config_conflict'):
        with tempfile.TemporaryDirectory() as temporary:
            out, files = restart_prepared(Path(temporary)); console = RestartConsole(files)
            if failure == 'claimed':
                key = update.load(out, 'plan.json')['run_id']
                console.files[update.APP + '/support/backend-restart-' + key + '.claimed.json'] = b'owned'
            elif failure == 'config_conflict': console.files[update.CAPTURE_CONFIG] = b'owned'
            else:
                from test_pctl_standard_evidence import raw_rows, encode
                rows = raw_rows()[:2]
                rows[0].update(release_id='new', boot_id='a' * 16)
                rows[1]['boot_id'] = 'a' * 16
                data = encode(rows) + (b'{' if failure == 'unknown_capture_tail' else b'')
                console.files[update.APP + '/support/usage-evidence-' + '0' * 32 + '-' + 'a' * 16 + '.jsonl'] = data
            try: run_case(console, out, Clock())
            except (update.UpdateError, ValueError): pass
            else: raise AssertionError('unowned capture state accepted: ' + failure)
            require(not console.actions and not console.writes, 'capture conflict is rejected before app exit or mutation')



def test_capture_restart_prepare_gates():
    for failure in ('old_candidate', 'dirty', 'wrong_zip', 'wrong_component', 'short_lease'):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); out, files = restart_prepared(root)
            path = root / 'package.zip'; capture = root / 'capture'
            if failure in ('old_candidate', 'dirty'):
                manifest = json.loads(files['switch/playwise/build.json'])
                if failure == 'old_candidate': manifest.pop('capture_restart_version')
                else: manifest['build']['source_dirty'] = True
                files['switch/playwise/build.json'] = json.dumps(manifest).encode()
                with zipfile.ZipFile(path, 'w') as archive:
                    for name, data in files.items(): archive.writestr(name, data)
            else:
                plan = update.load(capture, 'plan.json')
                if failure == 'wrong_zip': plan['zip_sha256'] = {'package.zip': '0' * 64}
                if failure == 'wrong_component': plan['release_components'][update.COMPONENTS[0]] = '0' * 64
                if failure == 'short_lease':
                    plan['config']['duration_s'] = 300
                    update.dump(capture / 'usage-evidence.json', plan['config'])
                update.dump(capture / 'plan.json', plan)
            with mock.patch.object(update, 'verify_package_zip'):
                try:
                    update.prepare(path, root / 'rejected', host='test.invalid', forwarder='0123456789ABCDEF',
                                   capture_plan=capture / 'plan.json')
                except update.UpdateError: pass
                else: raise AssertionError('unsafe restart preparation passed: ' + failure)
            require(not (root / 'rejected').exists(), 'unsafe restart creates no output or device transaction')


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
    test_capture_restart()
    test_capture_restart_prepare_gates()
    print("PASS bounded auto update, no replay, preimages, PIN privacy and offline evidence")


if __name__ == "__main__":
    main()
