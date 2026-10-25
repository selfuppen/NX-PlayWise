"""Isolated, bounded Eden UI/business smoke tests and offline evidence verification."""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.lib.ptc_token_v2 import encode_token, tier_for_minutes

ROOT = Path(__file__).resolve().parents[2]
APP = Path("sdmc/switch/playwise-eden")
SCENARIOS = ("setup", "reopen", "pin_cancel", "pin_wrong", "pin_correct", "code_success", "code_replay")


def scenario_names(suite: str) -> tuple[str, ...]:
    require(suite in ("smoke", "main", "support", "web", "plans"), "unknown suite")
    if suite == "plans":
        from tools.automation.eden_plan_flow import PLAN_SCENARIOS
        return PLAN_SCENARIOS
    if suite == "web":
        from tools.automation.eden_web_flow import WEB_SCENARIOS
        return WEB_SCENARIOS
    if suite == "support":
        from tools.automation.eden_support_flow import SUPPORT_SCENARIOS
        return SUPPORT_SCENARIOS
    if suite == "main":
        from tools.automation.eden_main_flow import MAIN_SCENARIOS
        return SCENARIOS + MAIN_SCENARIOS
    return SCENARIOS


class GateError(RuntimeError):
    pass


class ObservationPending(GateError):
    """A valid observation needs a new heartbeat before any input is allowed."""


def require(ok: bool, message: str) -> None:
    if not ok:
        raise GateError(message)


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def decode(text: str) -> dict:
    def pairs(items: list[tuple[str, object]]) -> dict:
        result: dict = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key")
            result[key] = value
        return result
    result = json.loads(text, object_pairs_hook=pairs,
                        parse_constant=lambda _: (_ for _ in ()).throw(GateError("non-finite JSON number")))
    require(isinstance(result, dict), "JSON object required")
    return result


def read(path: Path) -> dict:
    return decode(path.read_text(encoding="utf-8-sig"))


def open_shared_read(path: Path):
    """Do not block the emulator's delete/rename while observing its SD file."""
    if os.name != "nt":
        return path.open("rb")
    import ctypes
    from ctypes import wintypes
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    create.restype = wintypes.HANDLE
    # GENERIC_READ, FILE_SHARE_READ|WRITE|DELETE, OPEN_EXISTING.
    handle = create(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except Exception:
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel.CloseHandle(handle)
        raise
    return os.fdopen(descriptor, "rb")


def read_observation_file(path: Path) -> tuple[dict, float]:
    with open_shared_read(path) as source:
        raw = source.read(65537)
        modified = os.fstat(source.fileno()).st_mtime
    require(len(raw) <= 65536, "UI observation exceeds bounded size")
    return decode(raw.decode("utf-8-sig")), modified


def read_observation_stream(path: Path) -> tuple[dict, float]:
    with open_shared_read(path) as source:
        info = os.fstat(source.fileno())
        source.seek(max(0, info.st_size - 65536))
        raw = source.read(65536)
    # An unfinished trailing record is not a published observation. The
    # preceding complete line must parse; never search backwards past bad JSON.
    if b"\n" not in raw:
        raise ObservationPending("no complete observation stream record")
    complete = raw.rsplit(b"\n", 1)[0].rsplit(b"\n", 1)[-1]
    return decode(complete.decode("utf-8-sig")), info.st_mtime


def save(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, ensure_ascii=False, indent=2)


def healthy_launch(status: dict) -> None:
    require(status.get("running") is True and status.get("backend") == "eden-control" and
            status.get("launch", {}).get("target_reached") is True and
            status.get("launch", {}).get("phase") == "game_ready" and
            type(status.get("pid")) is int, "NRO/control readiness failed; input prohibited")


def healthy_exit(pid: int, reply: dict, audit: dict) -> None:
    require(reply.get("stopped") is True and reply.get("pid") == pid and reply.get("return_code") == 0,
            "owned emulator did not report a successful stop")
    require(audit.get("query_ok") is True, "Windows event audit unavailable; exit health unknown")
    require(not any(row.get("pid") == pid for row in audit.get("crashes", [])),
            "PID-bound Windows crash overrides the zero exit code")


def validate_observation(value: dict, release: str) -> None:
    require(type(value.get("schema_version")) is int and value["schema_version"] == 1 and
            value.get("release_id") == release and isinstance(value.get("instance"), str) and
            re.fullmatch(r"[0-9a-f]+", value["instance"]) is not None and
            type(value.get("sequence")) is int and value["sequence"] > 0 and
            isinstance(value.get("ui"), dict), "invalid/stale UI observation identity")
    require(not ({"pin_text", "pending_code", "grant_secret", "numpad_text"} & set(value["ui"])),
            "sensitive UI text must not be exported")


def receipt(path: Path, request_type: str, excluded: set[str]) -> dict:
    value = read(path)
    require(value.get("request_id") == path.stem and path.stem not in excluded and
            value.get("type") == request_type, "receipt ID/type or freshness differs")
    return value


def validate_native_environment(log: str) -> dict:
    denied = [line for line in log.splitlines() if re.search(r'\.nca\b.*permission denied', line, re.I)]
    fallback = re.findall(r"SynthesizeSystemArchive:.*?'(Font(?:Standard|ChineseSimple|ChineseTraditional))'", log)
    require(not denied, 'Eden cannot open installed firmware NCA; input prohibited')
    require(not fallback, 'Eden synthesized a required system font instead of using firmware; input prohibited')
    return {'schema_version': 1, 'firmware_nca_permission_errors': 0, 'synthesized_required_fonts': []}


class McpClient:
    """JSON-RPC stdio client; transport success never implies application success."""
    def __init__(self, command: list[str], stderr: Path, output: Path) -> None:
        self.log = stderr.open("x", encoding="utf-8")
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, encoding="utf-8", bufsize=1)
        self.messages: queue.Queue = queue.Queue()
        self.output, self.counter = output, 0
        threading.Thread(target=self._read, daemon=True).start()
        self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                     "clientInfo": {"name": "playwise-eden-runner", "version": "1"}})
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _read(self) -> None:
        try:
            while True:
                line = self.process.stdout.readline(32 * 1024 * 1024)
                if not line:
                    raise GateError("MCP server ended stdout")
                require(line.endswith("\n"), "MCP response exceeds bounded framing")
                self.messages.put(decode(line))
        except Exception as exc:
            self.messages.put(exc)

    def _send(self, value: dict) -> None:
        self.process.stdin.write(json.dumps(value) + "\n")
        self.process.stdin.flush()

    def request(self, method: str, params: dict, timeout: float = 30) -> dict:
        self.counter += 1
        number = self.counter
        self._send({"jsonrpc": "2.0", "id": number, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while True:
            try:
                reply = self.messages.get(timeout=max(0.001, deadline - time.monotonic()))
            except queue.Empty as exc:
                raise GateError("MCP timeout; request outcome unknown, no replay") from exc
            if isinstance(reply, Exception):
                raise reply
            if reply.get("id") != number:
                require("method" in reply and "id" not in reply, "unexpected MCP response ID")
                continue
            require("error" not in reply, f"MCP protocol error: {reply.get('error')}")
            result = reply["result"]
            for index, block in enumerate(result.get("content", [])):
                if block.get("type") == "image":
                    raw = base64.b64decode(block.pop("data"), validate=True)
                    require(raw[:8] == b"\x89PNG\r\n\x1a\n", "native screenshot is not PNG")
                    name = f"screens/{number:04d}-{index}.png"
                    target = self.output / name
                    target.parent.mkdir(exist_ok=True)
                    target.write_bytes(raw)
                    block.update(path=name, sha256=digest(target), width=struct.unpack(">I", raw[16:20])[0],
                                 height=struct.unpack(">I", raw[20:24])[0])
            save(self.output / f"calls/{number:04d}.json", {"method": method, "params": params, "result": result})
            return result

    def tool(self, name: str, arguments: dict | None = None, timeout: float = 30) -> dict:
        result = self.request("tools/call", {"name": name, "arguments": arguments or {}}, timeout)
        require(not result.get("isError", False), f"MCP tool rejected {name}: {result.get('content')}")
        if "structuredContent" in result:
            return result["structuredContent"]
        texts = [block["text"] for block in result.get("content", []) if block.get("type") == "text"]
        return decode(texts[0]) if texts else result

    def close(self) -> None:
        self.process.stdin.close()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.terminate()  # Only the Python MCP process this client created.
            self.process.wait(timeout=5)
        self.log.close()


WINDOWS_EVENTS = r"""
$ErrorActionPreference='Stop'
$since=[DateTimeOffset]::Parse($env:PLAYWISE_AUDIT_SINCE).LocalDateTime
$rows=@();$errorText=$null
try {
 $events=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=$since} -MaxEvents 256 -ErrorAction Stop)
 foreach($e in $events) {
  $v=@($e.Properties | ForEach-Object {$_.Value})
  if($v.Count -ge 11 -and $v[10] -eq $env:PLAYWISE_AUDIT_EXE) {
   $xml=Join-Path $env:PLAYWISE_AUDIT_DIR ($e.RecordId.ToString()+'.xml')
   if(-not(Test-Path -LiteralPath $xml)){[IO.File]::WriteAllText($xml,$e.ToXml(),[Text.UTF8Encoding]::new($false))}
   $rows+=@{pid=[Convert]::ToInt32($v[8],16);module=$v[3];exception=$v[6];time=$e.TimeCreated.ToString('o');xml=('windows/'+$e.RecordId.ToString()+'.xml')}
  }
 }
} catch {if($_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound*'){$errorText=$_.Exception.Message}}
@{query_ok=($null -eq $errorText);error=$errorText;crashes=@($rows)} | ConvertTo-Json -Depth 5 -Compress
"""


def windows_events(executable: Path, since: str, output: Path) -> dict:
    require(os.name == "nt", "live Eden runner currently requires Windows event auditing")
    folder = output / "windows"
    folder.mkdir(exist_ok=True)
    environment = dict(os.environ, PLAYWISE_AUDIT_SINCE=since, PLAYWISE_AUDIT_EXE=str(executable),
                       PLAYWISE_AUDIT_DIR=str(folder))
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", WINDOWS_EVENTS],
                            env=environment, text=True, encoding="utf-8", errors="replace",
                            capture_output=True, timeout=20)
    if result.returncode:
        return {"query_ok": False, "error": result.stderr, "crashes": []}
    try:
        return decode(result.stdout)
    except (ValueError, GateError):
        return {"query_ok": False, "error": "event audit output could not be parsed", "crashes": []}


def profile_settings(text: str) -> dict:
    """Non-secret settings only; numeric renderer values remain version-specific."""
    result = {}
    for key in ("backend", "language_index", "region_index", "time_zone_index"):
        value = re.search(rf"(?m)^{key}=(\d+)\r?$", text)
        default = re.search(rf"(?m)^{key}\\default=(true|false)\r?$", text)
        require(value is not None and default is not None, f"missing profile setting {key}")
        result[key] = int(value.group(1))
        result[key + "_default"] = default.group(1) == "true"
    return result


def set_profile_timezone(text: str, index: int | None) -> str:
    if index is None:
        return text
    require(0 <= index <= 255, "time zone index outside bounded enum range")
    profile_settings(text)
    text = re.sub(r"(?m)^time_zone_index=.*$", f"time_zone_index={index}", text)
    return re.sub(r"(?m)^time_zone_index\\default=.*$", r"time_zone_index\\default=false", text)


def check_runtime_capacity(root: Path, copy_bytes: int) -> dict:
    require(type(copy_bytes) is int and copy_bytes >= 0, "invalid profile copy size")
    root.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(root).free
    reserve = 512 * 1024 * 1024
    require(free >= copy_bytes + reserve,
            f"runtime disk space insufficient: free={free}, copy={copy_bytes}, reserve={reserve}; no profile copied")
    return {"copy_bytes": copy_bytes, "reserve_bytes": reserve, "free_before": free}


def prepare(args: argparse.Namespace, output: Path) -> tuple[Path, dict]:
    from tools.package_remote import verify_eden_nro
    manifest = read(args.manifest)
    verify_eden_nro(args.nro, manifest)
    runtime_mode = getattr(args, "runtime_mode", "copy")
    if runtime_mode == "shared":
        from tools.automation import eden_runtime
        application_root = args.application_root or Path(args.eden.anchor) / "PlayWise-Eden-AppLinks/managed"
        firmware_cache = args.firmware_cache or args.runtime_root.parent / "shared-resources/firmware/Contents"
        runtime, profile, ownership = eden_runtime.prepare(args.eden, args.profile, args.runtime_root,
                                                         application_root, firmware_cache, output)
        capacity = ownership["runtime_capacity"]
    else:
        sources = [args.eden]
        for name in ("config", "keys", "nand"):
            require((args.profile / name).is_dir(), f"profile component missing: {name}")
            sources.extend(p for p in (args.profile / name).rglob("*") if p.is_file())
        languages = args.eden.parent / "languages"
        if languages.is_dir():
            sources.extend(p for p in languages.rglob("*") if p.is_file())
        capacity = check_runtime_capacity(args.runtime_root.resolve(), sum(p.stat().st_size for p in sources))
        runtime = args.runtime_root.resolve() / uuid.uuid4().hex
        profile = runtime / "user"
        profile.mkdir(parents=True, exist_ok=False)
        shutil.copy2(args.eden, runtime / "eden.exe")
        for name in ("config", "keys", "nand"):
            shutil.copytree(args.profile / name, profile / name, symlinks=False)
        if languages.is_dir():
            shutil.copytree(languages, runtime / "languages")
    # Never seed the operator's SD or PlayWise authentication state.
    config = profile / "config/qt-config.ini"
    text = config.read_text(encoding="utf-8-sig")
    source_config_sha256 = digest(args.profile / "config/qt-config.ini")
    source_settings = profile_settings(text)
    for key, folder in [("nand_directory", "nand"), ("sdmc_directory", "sdmc"),
                        ("load_directory", "load"), ("dump_directory", "dump"),
                        ("tas_directory", "tas"), ("Screenshots\\screenshot_path", "screenshots")]:
        require(re.search(rf"(?m)^{re.escape(key)}=", text) is not None, f"missing profile path {key}")
        text = re.sub(rf"(?m)^{re.escape(key)}=.*$", lambda _: f"{key}={(profile / folder).as_posix()}", text)
    text = re.sub(r"(?m)^check_for_updates=.*$", "check_for_updates=false", text)
    text = re.sub(r"(?m)^check_for_updates\\default=.*$", r"check_for_updates\\default=false", text)
    if args.renderer_backend is not None:
        require(re.search(r"(?m)^backend=", text) is not None, "missing renderer backend setting")
        text = re.sub(r"(?m)^backend=.*$", f"backend={args.renderer_backend}", text)
        text = re.sub(r"(?m)^backend\\default=.*$", r"backend\\default=false", text)
    text = set_profile_timezone(text, args.time_zone_index)
    config.write_text(text, encoding="utf-8")
    save(output / "fixtures/profile-settings.json", {"source": source_settings,
         "effective": profile_settings(text), "time_zone_override": args.time_zone_index})
    save(output / "fixtures/renderer.json", {"requested_override": args.renderer_backend,
         "backend": int(re.search(r"(?m)^backend=(\d+)$", text).group(1)),
         "config_sha256": digest(config)})
    app = profile / APP
    (app / "support").mkdir(parents=True)
    (app / "support/automation-observe.flag").write_text("eden-runner-v1\n", encoding="ascii")
    if getattr(args, "trace_progress", False):
        (app / "support/automation-trace.flag").write_text("eden-progress-v1\n", encoding="ascii")
    rules = {"version": 1, "week": [{"mode": "limit", "minutes": 120} for _ in range(7)],
             "today_override_present": False, "today_override_day_index": 0,
             "today_override_mode": "limit", "today_override_minutes": 120}
    if args.suite == "main":
        rules.update(version=2, eye_care_enabled=False, eye_care_play_minutes=1, eye_care_rest_minutes=1,
                     holiday_enabled=False, holiday_mode="limit", holiday_minutes=90,
                     bedtime_enabled=False, bedtime_calendar_enabled=False,
                     bedtime_week=[{"enabled": True, "start_minute": 1, "end_minute": 0} for _ in range(7)],
                     bedtime_holiday_mode="inherit", bedtime_makeup_mode="inherit", bedtime_scheduled_present=False)
        year = datetime.now(timezone.utc).year
        calendar = {"format_version": 1, "region_id": "EDEN", "region_name": "Eden test year",
                    "year": year, "holidays": [{"name": "Test holiday", "from": f"{year}-01-01", "to": f"{year}-12-31"}],
                    "workdays": []}
        calendar_text = json.dumps(calendar, ensure_ascii=False)
        calendar_hash = hashlib.sha256(calendar_text.encode("utf-8")).hexdigest()
        entry = {"region_id": "EDEN", "region_name": "Eden test year", "year": year, "sha256": calendar_hash}
        library = app / f"calendars/library/EDEN-{year}-{calendar_hash}.json"
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_text(calendar_text, encoding="utf-8")
        save(app / "calendars/catalog.json", {"entries": [entry]})
        save(app / "calendars/active.json", {"option_id": "EDEN", "entries": [entry]})
        save(output / "fixtures/calendar.json", calendar)
    if args.suite == "plans":
        from tools.automation.eden_plan_flow import prepare_plans
        prepare_plans(app, output, rules)
    save(app / "rules.json", rules)
    save(output / "fixtures/rules.json", rules)
    artifacts = output / "artifacts"
    artifacts.mkdir()
    shutil.copy2(args.nro, artifacts / "pctc-eden.nro")
    shutil.copy2(args.manifest, artifacts / "manifest.json")
    for name in ("eden_runner.py", "eden_mcp_bridge.py"):
        shutil.copy2(Path(__file__).with_name(name), artifacts / name)
    if args.suite in ("main", "support", "web", "plans"):
        shutil.copy2(Path(__file__).with_name("eden_main_flow.py"), artifacts / "eden_main_flow.py")
    if args.suite == "support":
        shutil.copy2(Path(__file__).with_name("eden_support_flow.py"), artifacts / "eden_support_flow.py")
    if args.suite == "plans":
        shutil.copy2(Path(__file__).with_name("eden_plan_flow.py"), artifacts / "eden_plan_flow.py")
    if args.suite == "web":
        for name in ("eden_web_flow.py", "eden_web_browser.js"):
            shutil.copy2(Path(__file__).with_name(name), artifacts / name)
        shutil.copytree(ROOT / "tools/ptc_frontend", artifacts / "frontend")
    if runtime_mode == "shared":
        for name in ("eden_runtime.py", "eden_runtime_cleanup.ps1"):
            shutil.copy2(Path(__file__).with_name(name), artifacts / name)
    config_text = "[portable]\n" + "\n".join(f"{k} = {json.dumps(v)}" for k, v in {
        "root": runtime.as_posix(), "executable": "eden.exe", "profile_template": "user",
        "session_root": (profile.parent / "sessions").as_posix(),
    }.items()) + "\nrequire_control = true\nauto_prune_sessions = false\n"
    (runtime / "eden-mcp.toml").write_text(config_text, encoding="utf-8")
    identity = {"schema_version": 1, "run_id": runtime.name, "started_at": datetime.now(timezone.utc).isoformat(),
                "runtime_capacity": capacity, "runtime_mode": runtime_mode,
                "nro_sha256": digest(artifacts / "pctc-eden.nro"), "eden_sha256": digest(runtime / "eden.exe"),
                "manifest": manifest, "runtime": str(runtime), "profile": str(profile),
                "source_executable": str(args.eden.resolve()), "source_profile": str(args.profile.resolve()),
                "source_config_sha256": source_config_sha256,
                "launched_executable": str(runtime / "eden.exe"),
                "runner_sha256": digest(artifacts / "eden_runner.py"),
                "bridge_sha256": digest(artifacts / "eden_mcp_bridge.py"),
                "scope": "Eden simulated UI/business only; no production qualification"}
    if args.suite == 'plans':
        identity['plans_language'] = getattr(args, 'plans_language', 'system')
    save(output / "identity.json", identity)
    return runtime, identity


class Suite:
    def __init__(self, client: McpClient, output: Path, runtime: Path, identity: dict, seconds: int,
                 suite: str = "smoke") -> None:
        self.client, self.output, self.runtime, self.identity = client, output, runtime, identity
        self.app = runtime / "user" / APP
        self.deadline = time.monotonic() + seconds
        self.pid: int | None = None
        self.step = 0
        self.suite = suite
        self.results = {name: {"status": "not_executed"} for name in scenario_names(suite)}
        self.health: list[dict] = []

    def bounded(self) -> None:
        require(time.monotonic() < self.deadline and self.step < (500 if self.suite in ("main", "plans") else 180),
                "suite time/action limit reached")

    def observe(self) -> dict:
        self.bounded()
        stream = self.app / "support/automation-ui.jsonl"
        path = stream if stream.is_file() else self.app / "support/automation-ui.json"
        # Windows may briefly deny reads while Eden atomically replaces the
        # snapshot. Retry this read only; never replay an input or transaction.
        deadline = min(self.deadline, time.monotonic() + 1)
        while True:
            try:
                value, modified = read_observation_stream(path) if path == stream else read_observation_file(path)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.02)
        validate_observation(value, self.identity["manifest"]["release_id"])
        age = time.time() - modified
        require(age >= 0, "UI observation timestamp is in the future; input prohibited")
        if age >= 4:
            raise ObservationPending("UI observation heartbeat stale; input prohibited")
        return value

    def wait_ui(self, **expected: object) -> dict:
        deadline = min(self.deadline, time.monotonic() + 20)
        while time.monotonic() < deadline:
            try:
                value = self.observe()
                if not value["ui"].get("overlay_animating", True) and all(value["ui"].get(key) == item for key, item in expected.items()):
                    save(self.output / f"observations/{self.step:03d}-{uuid.uuid4().hex[:8]}.json", value)
                    return value
            except (OSError, ValueError, ObservationPending):
                pass
            time.sleep(0.1)
        raise GateError(f"UI precondition timeout: {expected}; no dependent input")

    def action(self, button: str | None = None, target: str | None = None, hold_ms: int = 200) -> None:
        self.bounded()
        healthy_launch(self.client.tool("eden_status"))
        value = self.wait_action_ready()
        self.step += 1
        require(200 <= hold_ms <= 1400, "input hold exceeds bound")
        name = "eden_press_button" if button else "eden_touch_screen"
        if button:
            arguments = {"button": button}
        else:
            geometry = value["ui"]["targets"]
            x, y = geometry[target]
            arguments = {"x": x, "y": y, "source_width": geometry["width"], "source_height": geometry["height"]}
        # One down, then one up even on failure. Tool acknowledgement is not
        # guest sampling; the observer must see the active input and release.
        pressed_at = time.monotonic()
        try:
            self.client.tool(name, {**arguments, "action": "down"})
            sampled = self.wait_input(True, after=value)
            if button:
                remaining = hold_ms / 1000 if hold_ms >= 1000 else max(0, hold_ms / 1000 - (time.monotonic() - pressed_at))
                time.sleep(remaining)
        finally:
            self.client.tool(name, {**arguments, "action": "up"})
        self.wait_input(False, after=sampled)
        time.sleep(0.15)

    def wait_action_ready(self) -> dict:
        # A cached neutral snapshot may precede a slow first render of a new
        # dialog. Require two progressing, presented idle frames before down.
        deadline = min(self.deadline, time.monotonic() + 5)
        baseline = self.wait_input(False)
        previous = None
        ready = 0
        while time.monotonic() < deadline:
            try:
                value = self.observe()
                require(type(value.get("presentation_version")) is int and value["presentation_version"] == 1,
                        "NRO does not publish completed frames; input prohibited")
                require(type(value.get("input_sampling_version")) is int and value["input_sampling_version"] == 2 and
                        type(value.get("sequence")) is int and type(value.get("input_sequence")) is int and
                        type(value.get("input_active")) is bool and value["instance"] == baseline["instance"],
                        "invalid input readiness observation")
                # Independent HID publication can be ahead of the last UI
                # frame. Wait for UI to catch up; it is not an instance reset.
                if (value["input_sequence"] < baseline["input_sequence"] or value["input_active"] or
                        value["ui"]["waiting"] or value["ui"]["overlay_animating"]):
                    previous = None
                    ready = 0
                elif previous is None:
                    previous = value
                    samples = [value]
                elif value["sequence"] > previous["sequence"] and value["input_sequence"] > previous["input_sequence"]:
                    samples.append(value)
                    previous = value
                    ready += 1
                    if ready == 2:
                        for index, sample in enumerate(samples):
                            save(self.output / f"input-readiness/{self.step + 1:03d}-{index}.json", sample)
                        return value
            except (OSError, ValueError, ObservationPending):
                pass
            time.sleep(0.05)
        raise GateError("guest did not present two progressing neutral frames before input; no down sent")

    def observe_input(self) -> dict:
        self.bounded()
        value, modified = read_observation_stream(self.app / "support/automation-input.jsonl")
        require(type(value.get("schema_version")) is int and value["schema_version"] == 1 and
                type(value.get("input_stream_version")) is int and value["input_stream_version"] == 1 and
                type(value.get("input_sampling_version")) is int and value["input_sampling_version"] == 2 and
                type(value.get("sequence")) is int and value["sequence"] > 0 and
                type(value.get("input_sequence")) is int and value["input_sequence"] == value["sequence"] and
                type(value.get("input_active")) is bool and isinstance(value.get("instance"), str) and value["instance"] and
                isinstance(value.get("tick"), str) and value["tick"].isascii() and value["tick"].isdecimal(),
                "invalid independent HID observation; input prohibited")
        age = time.time() - modified
        require(age >= 0, "input observation timestamp is in the future")
        if age > 4:
            raise ObservationPending("input observation is stale")
        return value

    def wait_input(self, active: bool, after: dict | None = None) -> dict:
        deadline = min(self.deadline, time.monotonic() + 5)
        while time.monotonic() < deadline:
            try:
                value = self.observe_input()
                require(type(value.get("input_sampling_version")) is int and value["input_sampling_version"] == 2 and
                        type(value.get("input_sequence")) is int and value["input_sequence"] >= 0 and
                        type(value.get("input_active")) is bool,
                        "NRO lacks bounded input sampling; input prohibited")
                if after:
                    require(value["instance"] == after["instance"] and
                            (after.get("input_stream_version") != 1 or value["input_sequence"] >= after["input_sequence"]),
                            "input instance/sequence changed; input prohibited")
                if value["input_sequence"] > 0 and value["input_active"] is active and (not after or value["input_sequence"] > after["input_sequence"]):
                    save(self.output / f"input-samples/{self.step:03d}-{uuid.uuid4().hex[:8]}.json", value)
                    return value
            except (OSError, ValueError, ObservationPending):
                pass
            time.sleep(0.05)
        raise GateError("guest did not sample input/release within five seconds; no replay")

    def capture(self, name: str) -> None:
        value = self.observe()
        self.client.tool("eden_take_screenshot")
        value["screenshot_call"] = self.client.counter
        save(self.output / f"checkpoints/{name}.json", value)
        self.wait_capture_resume(name)

    def wait_capture_resume(self, name: str) -> None:
        # Screenshot RPC completion does not prove the guest resumed its HID
        # loop. Never reuse a cached neutral sample from before the capture.
        deadline = min(self.deadline, time.monotonic() + 20)
        baseline = None
        ready = 0
        while time.monotonic() < deadline:
            try:
                value = self.observe()
                if baseline is not None:
                    require(value["instance"] == baseline["instance"], "instance changed after screenshot")
                    if (value["input_sequence"] > baseline["input_sequence"] and
                            value["sequence"] > baseline["sequence"] and value["input_active"] is False and
                            value["ui"]["waiting"] is False):
                        ready += 1
                        save(self.output / f"capture-readiness/{name}-{ready}.json", value)
                        if ready == 2:
                            return
                        baseline = value
                else:
                    baseline = value
                    save(self.output / f"capture-readiness/{name}-baseline.json", value)
            except (OSError, ValueError, ObservationPending):
                pass
            time.sleep(0.05)
        raise GateError("guest did not resume two neutral heartbeats after screenshot; no dependent input")

    def start(self) -> None:
        self.bounded()
        observation = self.app / "support/automation-ui.json"
        stream = self.app / "support/automation-ui.jsonl"
        if stream.is_file():
            target = self.output / f"ui-streams/{uuid.uuid4().hex}.jsonl"
            target.parent.mkdir(exist_ok=True)
            shutil.copy2(stream, target)
            stream.unlink()
        progress = self.app / "support/automation-progress.jsonl"
        if progress.is_file():
            target = self.output / f"progress-streams/{uuid.uuid4().hex}.jsonl"
            target.parent.mkdir(exist_ok=True)
            shutil.copy2(progress, target)
            progress.unlink()
        input_stream = self.app / "support/automation-input.jsonl"
        if input_stream.is_file():
            target = self.output / f"input-streams/{uuid.uuid4().hex}.jsonl"
            target.parent.mkdir(exist_ok=True)
            shutil.copy2(input_stream, target)
            input_stream.unlink()
        observation.unlink(missing_ok=True)  # One owned transient snapshot, while emulator is stopped.
        remaining = self.deadline - time.monotonic()
        status = self.client.tool("eden_start_game", {"base_path": str(self.output / "artifacts/pctc-eden.nro"),
                                  "wait_until": "game_ready", "timeout_s": max(1, min(60, int(remaining)))},
                                  timeout=min(150, remaining))
        healthy_launch(status)
        self.pid = status["pid"]
        require(Path(status.get("effective_profile_root", "")).resolve() == (self.runtime / "user").resolve(),
                "MCP selected a different profile; input prohibited")
        native_log = (self.runtime / "user/log/eden_log.txt").read_text(encoding="utf-8-sig", errors="replace")
        for key, expected in (("ConfigDir", self.runtime / "user/config"),
                              ("NANDDir", self.runtime / "user/nand"),
                              ("SDMCDir", self.runtime / "user/sdmc")):
            matched = re.search(rf"(?m)LogSettings: {key}: (.+)\r?$", native_log)
            require(matched is not None and Path(matched.group(1).strip()).resolve() == expected.resolve(),
                    f"actual Eden {key} is not isolated; input prohibited")
        self.wait_ui()
        require(read(self.app / "build.json") == self.identity["manifest"], "fresh SD build identity differs")
        native_log = (self.runtime / "user/log/eden_log.txt").read_text(encoding="utf-8-sig", errors="replace")
        log_path = self.output / f"native-environment/{self.pid}.log"
        log_path.parent.mkdir(exist_ok=True)
        log_path.write_text(native_log, encoding="utf-8")
        report = validate_native_environment(native_log)
        save(log_path.with_suffix('.json'), {**report, 'pid': self.pid, 'log_sha256': digest(log_path)})
        self.capture(f"startup-{self.pid}")

    def stop(self) -> None:
        if self.pid is None:
            return
        pid, self.pid = self.pid, None
        reply = self.client.tool("eden_stop", timeout=30)
        time.sleep(8)  # Allow delayed WER events before sealing an exit verdict.
        audit = windows_events(Path(self.identity["launched_executable"]), self.identity["started_at"], self.output)
        record = {"pid": pid, "reply": reply, "events": audit}
        self.health.append(record)
        save(self.output / f"health/exit-{pid}.json", record)
        healthy_exit(pid, reply, audit)

    def ids(self) -> set[str]:
        return {path.stem for path in (self.app / "results").glob("*.json")}

    def wait_receipt(self, kind: str, before: set[str]) -> dict:
        deadline = min(self.deadline, time.monotonic() + 20)
        while time.monotonic() < deadline:
            found = []
            for path in (self.app / "results").glob("*.json"):
                if path.stem not in before:
                    try:
                        value = read(path)
                        if value.get("type") == kind:
                            found.append(receipt(path, kind, before))
                    except (OSError, ValueError):
                        pass
            require(kind == "status" or len(found) <= 1, "ambiguous business receipts; stop without replay")
            if found:
                if kind == "status":
                    # Automatic UI polling can overlap the explicit read. All
                    # are read-only: retain each, reject errors, select newest.
                    require(all(row.get("status") == "ok" and type(row.get("completed_at")) is int
                                and row["completed_at"] >= 0 for row in found), "invalid read-only status receipt")
                for row in found:
                    save(self.output / f"receipts/{row['request_id']}.json", row)
                value = max(found, key=lambda row: (row["completed_at"], row["request_id"])) if kind == "status" else found[0]
                return value
            time.sleep(0.1)
        raise GateError(f"{kind} receipt timeout; outcome unknown, no resubmission")

    def run_case(self, name: str, function) -> None:
        self.results[name] = {"status": "running"}
        try:
            function()
            self.capture(name)
            self.results[name] = {"status": "pass"}
        except Exception as exc:
            self.results[name] = {"status": "failed_or_unknown", "detail": str(exc)}
            try:
                metrics = self.client.tool("eden_get_runtime_metrics", {"sample_ms": 500}, timeout=20)
                save(self.output / f"failure-metrics/{name}.json", metrics)
            except Exception as diagnostic_error:
                try:
                    save(self.output / f"failure-metrics/{name}-error.json", {"error": str(diagnostic_error)})
                except OSError:
                    pass  # Diagnostics must not replace the original failure or cleanup.
            try:
                self.capture(name + "_failed")
            except Exception:
                pass  # Evidence failure never prevents owned-process cleanup.
            raise

    def setup(self) -> None:
        self.wait_ui(view=3, setup_step=1, overlay=0, waiting=False)
        self.action(target="setup_primary")
        self.wait_ui(view=3, setup_step=2, overlay=0)
        self.action(target="setup_primary")
        self.wait_ui(view=3, setup_step=3, overlay=0)
        before = self.ids()
        self.action(target="setup_primary")
        reply = self.wait_receipt("complete_setup", before)
        require(reply["status"] == "ok", "setup activation rejected")
        self.wait_ui(view=1, overlay=0, waiting=False, setup_active=True)
        require(read(self.app / "config.json")["setup_wizard_step"] == 0, "setup completion not persisted")

    def reopen(self) -> None:
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0, waiting=False)
        require(read(self.app / "config.json")["setup_wizard_step"] == 0, "reopen lost setup completion")

    def enter_pin(self, digits: str) -> None:
        for index, digit in enumerate(digits):
            self.wait_ui(overlay=22, pin_length=index)
            self.action(target=f"pin_{digit}")
        self.wait_ui(overlay=22, pin_length=len(digits))
        self.action(target="pin_confirm")

    def pin_open(self) -> None:
        self.wait_ui(view=0, overlay=0, waiting=False)
        self.action(button="Minus")
        self.wait_ui(overlay=22, pin_length=0)

    def pin_cancel(self) -> None:
        auth_path = self.app / "auth.json"
        before_bytes = auth_path.read_bytes()
        before = decode(before_bytes.decode("utf-8"))
        self.pin_open()
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        require(auth_path.read_bytes() == before_bytes, "cancel changed authentication storage")
        save(self.output / "assertions/pin_cancel.json", {"before": before["failed_attempts"],
             "after": read(self.app / "auth.json")["failed_attempts"], "auth_bytes_unchanged": True})

    def pin_wrong(self) -> None:
        self.pin_open()
        self.enter_pin("000")
        self.wait_ui(view=0, overlay=13)
        require(read(self.app / "auth.json")["failed_attempts"] == 1, "wrong PIN not recorded")
        save(self.output / "assertions/pin_wrong.json", {"failed_attempts": 1})
        self.capture("pin_wrong_error")
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)

    def pin_correct(self) -> None:
        self.pin_open()
        self.enter_pin("110")  # Fixed default created through the fresh Eden setup UI.
        self.wait_ui(view=1, overlay=0)
        require(read(self.app / "auth.json")["failed_attempts"] == 0, "correct PIN did not clear failures")
        self.capture("pin_correct_parent")
        save(self.output / "assertions/pin_correct.json", {"failed_attempts": 0})
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)

    def type_code(self, code: str) -> set[str]:
        self.wait_ui(view=0, overlay=0, waiting=False)
        self.action(target="child_code")
        self.wait_ui(overlay=4, numpad_length=0)
        for index, digit in enumerate(code):
            self.wait_ui(overlay=4, numpad_length=index)
            self.action(target=f"code_{digit}")
        self.wait_ui(overlay=4, numpad_length=8)
        before = self.ids()
        self.action(button="Plus")
        return before

    def code_success(self) -> None:
        statuses = [read(path) for path in (self.app / "results").glob("*.json")]
        latest = max((row for row in statuses if row.get("type") == "status" and row.get("status") == "ok"),
                     key=lambda row: row["completed_at"])
        self.code_day = latest["state"]["day_index"]
        self.code = encode_token(tier_for_minutes(30), 0, "eden-switch", "playwise-eden-test-secret-00000001", self.code_day)
        before = self.type_code(self.code)
        preview = self.wait_receipt("preview_offline_code", before)
        require(preview["status"] == "ok", "grant preview rejected")
        require(preview["preview"]["effective_add_minutes"] == 30, "grant preview amount differs")
        self.wait_ui(overlay=3, waiting=False)
        before = self.ids()
        self.action(button="A")
        self.grant = self.wait_receipt("offline_code", before)
        require(self.grant["status"] == "ok", "grant transaction failed")
        self.wait_ui(overlay=12, code_result_failed=False, code_result_pending=False)
        self.ledger = (self.app / "ledger/used_nonces.jsonl").read_bytes()
        entries = [decode(line) for line in self.ledger.decode("utf-8").splitlines()]
        require(len(entries) == 1 and entries[0]["nonce"] == 0 and entries[0]["day_index"] == self.code_day and
                entries[0]["token_version"] == 2,
                "successful redemption did not persist exactly the expected nonce")
        self.rules_after_grant = (self.app / "rules.json").read_bytes()
        rules = decode(self.rules_after_grant.decode("utf-8"))
        require(rules["today_override_present"] is True and rules["today_override_day_index"] == self.code_day and
                rules["today_override_minutes"] == 150, "30-minute grant did not change the 120-minute fixture to 150")
        require(preview["preview"]["remaining_after_minutes"] - 1 <= self.grant["state"]["remaining_minutes"] <=
                preview["preview"]["remaining_after_minutes"], "post-grant balance differs from preview by more than one simulated minute")
        activity = [decode(line) for line in (self.app / "activity/history.jsonl").read_text(encoding="utf-8").splitlines()]
        require(sum(row["action"] == "offline_grant" and row["minutes"] == 30 and row["effective_minutes"] == 30
                    for row in activity) == 1, "successful grant activity missing or duplicated")
        save(self.output / "assertions/grant.json", {"day_index": self.code_day, "nonce": 0,
             "grant": self.grant, "ledger_sha256": hashlib.sha256(self.ledger).hexdigest()})

    def code_replay(self) -> None:
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        before = self.type_code(self.code)
        denied = self.wait_receipt("preview_offline_code", before)
        require(denied["status"] == "error" and denied.get("error", {}).get("code") == 206,
                "replay was not explicitly rejected as used token")
        require((self.app / "ledger/used_nonces.jsonl").read_bytes() == self.ledger, "rejected replay changed nonce ledger")
        require((self.app / "rules.json").read_bytes() == self.rules_after_grant, "rejected replay changed rules")
        self.wait_ui(overlay=4, waiting=False)

    def execute(self) -> None:
        self.start()
        for name in scenario_names(self.suite):
            self.run_case(name, getattr(self, name))


def seal(output: Path, summary: dict) -> None:
    save(output / "summary.json", summary)
    import html
    pictures = [p.relative_to(output).as_posix() for p in sorted((output / "screens").glob("*.png"))]
    gallery = '<!doctype html><meta charset="utf-8"><title>Eden automation</title><style>body{font:16px system-ui;max-width:1200px;margin:30px auto}img{width:100%}article{margin:30px 0}</style>'
    gallery += f"<h1>Eden automation: {html.escape(summary['overall'])}</h1>"
    gallery += "".join(f'<article><p>{html.escape(p)}</p><a href="{p}"><img src="{p}"></a></article>' for p in pictures)
    (output / "gallery.html").write_text(gallery, encoding="utf-8")
    files = [{"path": path.relative_to(output).as_posix(), "sha256": digest(path), "bytes": path.stat().st_size}
             for path in sorted(output.rglob("*")) if path.is_file() and path.name != "evidence-manifest.json"]
    save(output / "evidence-manifest.json", {"schema_version": 1, "files": files})


def verify(output: Path) -> dict:
    manifest = read(output / "evidence-manifest.json")
    require(manifest.get("schema_version") == 1 and isinstance(manifest.get("files"), list), "invalid evidence manifest")
    seen: set[str] = set()
    for row in manifest["files"]:
        path = (output / row["path"]).resolve()
        require(path.is_relative_to(output.resolve()) and row["path"] not in seen, "unsafe/duplicate evidence path")
        seen.add(row["path"])
        require(path.stat().st_size == row["bytes"] and digest(path) == row["sha256"], "evidence bytes changed")
    require("summary.json" in seen and "identity.json" in seen, "evidence identity/summary missing")
    summary = read(output / "summary.json")
    names = scenario_names(summary.get("suite", "smoke"))
    require(summary.get("schema_version") == 1 and set(summary["scenarios"]) == set(names), "invalid summary schema")
    if summary["overall"] == "pass":
        require(all(row["status"] == "pass" for row in summary["scenarios"].values()) and not summary["errors"] and
                len(summary["health"]) == (3 if summary.get("suite") == "main" else 2), "incomplete suite cannot pass")
        for row in summary["health"]:
            healthy_exit(row["pid"], row["reply"], row["events"])
        if summary.get("input_sampling_version") in (1, 2):
            verify_input_evidence(output, summary["actions"])
        if summary.get("native_environment_version") == 1:
            if read(output / 'identity.json').get('runtime_mode') == 'shared':
                from tools.automation.eden_runtime import fingerprint
                owner = read(output / 'runtime-ownership.json')
                access = read(output / 'firmware-access.json')
                expected = fingerprint(owner['firmware_before'])
                require(type(access.get('schema_version')) is int and access['schema_version'] == 1 and
                        access.get('policy') == 'writable_handles_content_hash_guard_v1' and
                        access.get('cache') == owner['firmware_cache'] and
                        type(access.get('files_checked')) is int and access['files_checked'] == len(owner['firmware_before']) and
                        access.get('content_unchanged') is True and
                        access.get('content_fingerprint_before') == access.get('content_fingerprint_after') == expected and
                        isinstance(access.get('read_only_cleared'), list) and
                        len(set(access['read_only_cleared'])) == len(access['read_only_cleared']) and
                        set(access['read_only_cleared']) <= {r['path'] for r in owner['firmware_before']},
                        'firmware access evidence differs')
            for row in summary['health']:
                path = output / f"native-environment/{row['pid']}.log"
                report = read(path.with_suffix('.json'))
                require(report == {**validate_native_environment(path.read_text(encoding='utf-8')),
                                   'pid': row['pid'], 'log_sha256': digest(path)}, 'native environment evidence differs')
        if summary.get("capture_readiness_version") == 1:
            verify_capture_readiness(output)
        if summary.get("input_readiness_version") == 1:
            verify_input_readiness(output, summary["actions"])
        if summary.get("progress_trace_version") == 1:
            verify_progress_streams(output)
        if (output / "app-final/support/automation-ui.jsonl").is_file():
            verify_observation_streams(output)
        if summary.get("suite") == "support":
            from tools.automation.eden_support_flow import verify_support
            verify_support(output)
            return summary
        if summary.get("suite") == "web":
            from tools.automation.eden_web_flow import verify_web
            verify_web(output)
            return summary
        if summary.get("suite") == "plans":
            from tools.automation.eden_plan_flow import verify_plans
            verify_plans(output)
            return summary
        require(read(output / "checkpoints/setup.json")["ui"]["view"] == 1 and
                read(output / "checkpoints/reopen.json")["ui"]["view"] == 0 and
                read(output / "checkpoints/pin_correct_parent.json")["ui"]["view"] == 1,
                "setup/reopen/PIN page assertions differ")
        require(read(output / "assertions/pin_cancel.json") == {"before": 0, "after": 0, "auth_bytes_unchanged": True} and
                read(output / "assertions/pin_wrong.json")["failed_attempts"] == 1 and
                read(output / "assertions/pin_correct.json")["failed_attempts"] == 0, "PIN evidence differs")
        rows = [read(path) for path in (output / "receipts").glob("*.json")]
        for path in (output / "receipts").glob("*.json"):
            require(read(path)["request_id"] == path.stem, "archived receipt ID differs")
        require(sum(row.get("type") == "offline_code" and row.get("status") == "ok" for row in rows) == 1 and
                sum(row.get("type") == "preview_offline_code" and row.get("error", {}).get("code") == 206 for row in rows) == 1,
                "successful redemption/replay rejection evidence incomplete")
        if summary.get("suite") == "main":
            from tools.automation.eden_main_flow import verify_main
            verify_main(output)
        else:
            require(read(output / "app-final/rules.json")["today_override_minutes"] == 150,
                    "persisted allowance assertion differs")
        ledger = output / "app-final/ledger/used_nonces.jsonl"
        require(digest(ledger) == read(output / "assertions/grant.json")["ledger_sha256"], "nonce evidence differs")
    return summary


def verify_observation_streams(output: Path) -> None:
    samples = {"ui": {}, "input": {}}
    for path in list((output / "input-samples").glob("*.json")) + list((output / "input-readiness").glob("*.json")):
        value = read(path)
        group = samples["input" if value.get("input_stream_version") == 1 else "ui"]
        key = (value["instance"], value["sequence"])
        require(key not in group or group[key] == value, "same observation sequence has different input samples")
        group[key] = value
    for kind, group in samples.items():
        if not group:
            continue
        found = set()
        folder = "ui-streams" if kind == "ui" else "input-streams"
        name = "automation-ui.jsonl" if kind == "ui" else "automation-input.jsonl"
        streams = list((output / folder).glob("*.jsonl")) + [output / "app-final/support" / name]
        for path in streams:
            with path.open("rb") as source:
                while raw := source.readline(65537):
                    require(len(raw) <= 65536, "observation stream record exceeds bound")
                    if not raw.endswith(b"\n"):
                        require(source.read(1) == b"", "observation stream record is truncated before EOF")
                        break  # Preserve, but never accept, an interrupted trailing append.
                    value = decode(raw.decode("utf-8"))
                    key = (value["instance"], value["sequence"])
                    if key in group:
                        require(value == group[key], "input evidence differs from original observation stream")
                        found.add(key)
        require(found == group.keys(), "input samples absent from original observation streams")


def verify_input_readiness(output: Path, actions: int) -> None:
    require(type(actions) is int and 0 < actions <= 500, "invalid readiness action count")
    files = list((output / "input-readiness").glob("*.json"))
    require(len(files) == actions * 3, "input readiness samples missing or duplicated")
    for action in range(1, actions + 1):
        previous = None
        for index in range(3):
            value = read(output / f"input-readiness/{action:03d}-{index}.json")
            require(type(value.get("presentation_version")) is int and value["presentation_version"] == 1 and
                    type(value.get("input_sampling_version")) is int and value["input_sampling_version"] in (1, 2) and
                    type(value.get("sequence")) is int and type(value.get("input_sequence")) is int and
                    value["input_active"] is False and value["ui"]["waiting"] is False and
                    value["ui"]["overlay_animating"] is False, "invalid presented neutral frame")
            if previous:
                require(value["instance"] == previous["instance"] and value["sequence"] > previous["sequence"] and
                        value["input_sequence"] > previous["input_sequence"], "readiness frames did not advance")
            previous = value


def verify_progress_streams(output: Path) -> None:
    streams = list((output / "progress-streams").glob("*.jsonl")) + [output / "app-final/support/automation-progress.jsonl"]
    for path in streams:
        previous = None
        for raw in path.read_bytes().splitlines(keepends=True):
            if not raw.endswith(b"\n"):
                break
            value = decode(raw.decode("utf-8"))
            require(value.get("schema_version") == 1 and type(value.get("stage")) is int and 1 <= value["stage"] <= 16 and
                    type(value.get("detail")) is int and type(value.get("input_active")) is bool and
                    type(value.get("sequence")) is int and type(value.get("input_sequence")) is int and
                    isinstance(value.get("instance"), str) and isinstance(value.get("tick"), str) and value["tick"].isdigit(),
                    "invalid progress trace record")
            if previous:
                require(value["instance"] == previous["instance"] and value["sequence"] > previous["sequence"] and
                        value["input_sequence"] >= previous["input_sequence"], "progress trace regressed")
            previous = value
        require(previous is not None, "empty progress trace")


def verify_capture_readiness(output: Path) -> None:
    for point in (output / "checkpoints").glob("*.json"):
        baseline = read(output / f"capture-readiness/{point.stem}-baseline.json")
        for index in (1, 2):
            value = read(output / f"capture-readiness/{point.stem}-{index}.json")
            require(value["instance"] == baseline["instance"] and
                    type(value["sequence"]) is int and value["sequence"] > baseline["sequence"] and
                    type(value["input_sequence"]) is int and value["input_sequence"] > baseline["input_sequence"] and
                    value["input_active"] is False and value["ui"]["waiting"] is False,
                    "post-screenshot neutral heartbeat evidence differs")
            baseline = value


def verify_input_evidence(output: Path, actions: int) -> None:
    require(type(actions) is int and 0 < actions <= 500, "invalid action count")
    calls = [read(p)["params"] for p in sorted((output / "calls").glob("*.json"))]
    inputs = [p for p in calls if p.get("name") in ("eden_press_button", "eden_touch_screen")]
    require(len(inputs) == 2 * actions, "input down/up count differs")
    for down, up in zip(inputs[::2], inputs[1::2]):
        a, b = dict(down["arguments"]), dict(up["arguments"])
        require(a.pop("action") == "down" and b.pop("action") == "up" and a == b and
                down["name"] == up["name"], "unpaired/replayed input")
    groups: dict[int, list[dict]] = {}
    for path in (output / "input-samples").glob("*.json"):
        value = read(path)
        require(type(value.get("input_active")) is bool and type(value.get("input_sequence")) is int and
                value["input_sequence"] > 0, "invalid sampled input evidence")
        groups.setdefault(int(path.name.split("-", 1)[0]), []).append(value)
    for step in range(1, actions + 1):
        rows = groups.get(step, [])
        active = [r for r in rows if r["input_active"]]
        require(len(active) == 1 and any(not r["input_active"] and r["instance"] == active[0]["instance"] and
                r["input_sequence"] > active[0]["input_sequence"] for r in rows), "sampled input lacks later neutral release")


def run(args: argparse.Namespace) -> int:
    require(os.name == "nt", "live suite requires Windows; offline verify is portable")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    try:
        runtime, identity = prepare(args, output)
    except (GateError, OSError, ValueError) as exc:
        failure = {"schema_version": 1, "phase": "preparation", "status": "precondition_not_met",
                   "error": str(exc), "scenarios": {name: {"status": "not_executed"} for name in scenario_names(args.suite)}}
        save(output / "preparation.json", failure)
        if (output / "runtime-ownership.json").is_file():
            from tools.automation.eden_runtime import finish
            finish(output, False)
        print(json.dumps(failure, ensure_ascii=False))
        return 1
    command = [str(args.mcp_python), str(output / "artifacts/eden_mcp_bridge.py"),
               "--config", str(runtime / "eden-mcp.toml"), "--trace", str(output / "processes.jsonl"),
               "--native-dir", str(output)]
    if args.mcp_site:
        command.extend(["--site", str(args.mcp_site)])
    client = None
    suite = None
    errors = []
    try:
        client = McpClient(command, output / "server-stderr.log", output)
        suite_class = Suite
        if args.suite == "main":
            from tools.automation.eden_main_flow import MainFlow
            class MainSuite(MainFlow, Suite):
                pass
            suite_class = MainSuite
        elif args.suite == "support":
            from tools.automation.eden_support_flow import SupportFlow
            class SupportSuite(SupportFlow, Suite):
                pass
            suite_class = SupportSuite
        elif args.suite == "web":
            from tools.automation.eden_web_flow import WebFlow
            class WebSuite(WebFlow, Suite):
                pass
            suite_class = WebSuite
        elif args.suite == "plans":
            from tools.automation.eden_plan_flow import PlanFlow
            class PlanSuite(PlanFlow, Suite):
                pass
            suite_class = PlanSuite
        suite = suite_class(client, output, runtime, identity, args.timeout, args.suite)
        suite.execute()
    except Exception as exc:
        errors.append(str(exc))
    finally:
        if suite:
            try:
                suite.stop()
            except Exception as exc:
                errors.append(f"cleanup: {exc}")
        if client:
            try:
                client.tool("eden_stop", timeout=30)  # Also covers a failed launch with no ready PID.
            except Exception as exc:
                errors.append(f"final stop: {exc}")
            client.close()
    audit = windows_events(Path(identity["launched_executable"]), identity["started_at"], output)
    save(output / "final-events.json", audit)
    native_log = runtime / "user/log/eden_log.txt"
    if native_log.is_file():
        shutil.copy2(native_log, output / "eden-log.txt")
    save(output / "profile-settings-final.json",
         profile_settings((runtime / "user/config/qt-config.ini").read_text(encoding="utf-8-sig")))
    app = runtime / "user" / APP
    for name in ("config.json", "rules.json", "state.json", "build.json", "ledger/used_nonces.jsonl", "activity/history.jsonl",
                 "setup.json", "flags/disable.flag", "backups/install_pctl_snapshot.json", "support/automation-ui.jsonl",
                 "support/automation-progress.jsonl", "support/automation-input.jsonl"):
        path = app / name
        if path.is_file():
            target = output / "app-final" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    if (app / "auth.json").is_file():
        auth = read(app / "auth.json")
        save(output / "app-final/auth-counters.json", {key: auth[key] for key in ("failed_attempts", "cooldown_until")})
    if args.suite == "plans":
        for name in ("calendars", "calendar-import", "inbox"):
            if (app / name).is_dir():
                shutil.copytree(app / name, output / "app-final" / name)
        for path in (app / "support").glob("automation-request-*.json"):
            shutil.copy2(path, output / "app-final/support" / path.name)
    require_audit = audit.get("query_ok") and not audit.get("crashes")
    if not require_audit:
        errors.append("run contains Windows crash events or audit unavailable")
    if identity.get("runtime_mode") == "shared":
        from tools.automation.eden_runtime import verify_protected
        try:
            verify_protected(read(output / "runtime-ownership.json"))
        except (OSError, ValueError) as exc:
            errors.append(f"shared resource integrity: {exc}")
    summary = {"schema_version": 1, "overall": "pass" if not errors else "partial",
               "native_environment_version": 1,
               "input_sampling_version": 2, "capture_readiness_version": 1, "input_readiness_version": 1,
               "progress_trace_version": 1 if getattr(args, "trace_progress", False) else 0,
               "actions": suite.step if suite else 0,
               "suite": args.suite,
               "scenarios": suite.results if suite else {name: {"status": "not_executed"} for name in scenario_names(args.suite)},
               "health": suite.health if suite else [], "errors": errors,
               "scope": identity["scope"], "source_changes_do_not_qualify_real_device": True}
    seal(output, summary)
    try:
        require(verify(output) == verify(output) == summary, "offline serialization/repeat verification differs")
    except (RuntimeError, OSError, ValueError):
        if identity.get("runtime_mode") == "shared":
            from tools.automation.eden_runtime import finish
            finish(output, False)
        raise
    if identity.get("runtime_mode") == "shared":
        from tools.automation.eden_runtime import finish
        finish(output, summary["overall"] == "pass")
        require(verify(output) == verify(output) == summary, "evidence verification differs after runtime cleanup")
    print(json.dumps({"overall": summary["overall"], "scenarios": summary["scenarios"], "errors": errors, "output": str(output)}, ensure_ascii=False))
    return 0 if summary["overall"] == "pass" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    live = commands.add_parser("run")
    live.add_argument("--eden", type=Path, required=True)
    live.add_argument("--profile", type=Path, required=True)
    live.add_argument("--mcp-python", type=Path, required=True)
    live.add_argument("--mcp-site", type=Path)
    live.add_argument("--renderer-backend", type=int, choices=(0, 1),
                      help="optional Eden renderer enum override in the private profile only")
    live.add_argument("--time-zone-index", type=int,
                      help="optional system timezone enum override in the private profile only (PRC=34 in this build)")
    live.add_argument("--runtime-root", type=Path, default=ROOT / "build/eden-runtimes",
                      help="parent directory for fresh private profile data")
    live.add_argument("--runtime-mode", choices=("shared", "copy"), default="shared",
                      help="shared reuses executable/firmware and cleans verified successful runs; copy is legacy")
    live.add_argument("--application-root", type=Path,
                      help="shared exe hardlink entries on the source exe volume; default drive:/PlayWise-Eden-AppLinks/managed")
    live.add_argument("--firmware-cache", type=Path,
                      help="shared immutable Contents directory; validated against source, copied only if absent")
    live.add_argument("--nro", type=Path, default=ROOT / "build/eden-test/pctc-eden.nro")
    live.add_argument("--manifest", type=Path, default=ROOT / "build/eden-test/generated/release-manifest.json")
    live.add_argument("--out", type=Path, required=True)
    live.add_argument("--suite", choices=("smoke", "main", "support", "web", "plans"), default="smoke",
                      help="main covers core journeys; support covers diagnostics/disable/installation restore")
    live.add_argument('--plans-language', choices=('system', 'en'), default='system',
                      help='plans only: explicit English UI layout check through the normal language menu')
    live.add_argument("--timeout", type=int, default=300, choices=range(60, 1201), metavar="60..1200")
    live.add_argument("--trace-progress", action="store_true",
                      help="Enable isolated bounded numeric stage diagnostics; not a business assertion")
    offline = commands.add_parser("verify")
    offline.add_argument("--run", type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'run' and args.plans_language != 'system' and args.suite != 'plans':
        parser.error('--plans-language en requires --suite plans')
    try:
        if args.command == "verify":
            value = verify(args.run.resolve())
            if args.run.with_name(args.run.name + "-cleanup").joinpath("evidence-manifest.json").is_file():
                from tools.automation.eden_runtime import verify_cleanup
                verify_cleanup(args.run.resolve())
            print(f"PASS: evidence verification; recorded overall={value['overall']}")
            return 0
        return run(args)
    except (RuntimeError, OSError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
