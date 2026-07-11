#!/usr/bin/env python3
from __future__ import annotations

from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
from pathlib import Path
import shutil
import sys
import time
from typing import Any
from urllib.parse import parse_qs, urlparse
import zipfile

from grant_code import day_index_for, today_utc8
from ptc_request_queue import APP_DIR, AppPaths, create_layout, new_request_id, write_json_atomic, write_request
from ptc_token_v1 import MAX_NONCE, TOKEN_ACTION_ADD_TODAY_MINUTES, TOKEN_VERSION, TokenPayload, encode_token


REQUEST_TYPES_WITH_EMPTY_PAYLOAD = {
    "status",
    "disable_today_limit",
    "block_today",
    "restore_today_policy",
    "parent_unlock_end",
    "probe_play_timer_write",
    "probe_raw_block",
    "probe_suspend",
}


def app_paths(root: str | Path) -> AppPaths:
    return create_layout(Path(root))


def mask_secret(value: Any) -> Any:
    if not isinstance(value, str) or not value:
        return value
    if len(value) <= 8:
        return "***"
    return f"{value[:4]}...{value[-4:]}"


def read_json_or_none(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def tail_text(path: Path, limit: int = 20) -> list[str]:
    if not path.is_file():
        return []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines[-limit:]


def layout_health(root: str | Path) -> dict[str, Any]:
    paths = app_paths(root)
    required = [
        paths.app_root / "config.json",
        paths.app_root / "auth.json",
        paths.app_root / "rules.json",
        paths.app_root / "state.json",
        paths.app_root / "capabilities.json",
        paths.pending,
        paths.processing,
        paths.done,
        paths.results,
        paths.logs,
        paths.ledger,
        paths.backups,
        paths.flags,
    ]
    missing = [str(path.relative_to(paths.sdmc_root)) for path in required if not path.exists()]
    config = read_json_or_none(paths.app_root / "config.json")
    rules = read_json_or_none(paths.app_root / "rules.json")
    state = read_json_or_none(paths.app_root / "state.json")
    capabilities = read_json_or_none(paths.app_root / "capabilities.json")
    if config and "grant_secret" in config:
        config = dict(config)
        config["grant_secret"] = mask_secret(config["grant_secret"])
    return {
        "sdmc_root": str(paths.sdmc_root),
        "app_root": str(paths.app_root),
        "missing": missing,
        "disable_flag": (paths.flags / "disable.flag").exists(),
        "counts": {
            "pending": len(list(paths.pending.glob("*.json"))) if paths.pending.exists() else 0,
            "processing": len(list(paths.processing.glob("*.json"))) if paths.processing.exists() else 0,
            "done": len(list(paths.done.glob("*.json"))) if paths.done.exists() else 0,
            "results": len(list(paths.results.glob("*.json"))) if paths.results.exists() else 0,
        },
        "config": config,
        "rules": rules,
        "state": state,
        "capabilities": capabilities,
        "events_tail": tail_text(paths.logs / "events.jsonl"),
        "ledger_tail": tail_text(paths.ledger / "used_nonces.jsonl"),
        "backup_tail": tail_text(paths.backups / "last_pctl_backup.txt", 12),
        "recent_results": recent_results(paths),
    }


def recent_results(paths: AppPaths, limit: int = 8) -> list[dict[str, Any]]:
    if not paths.results.exists():
        return []
    result_files = sorted(paths.results.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)[:limit]
    results: list[dict[str, Any]] = []
    for path in result_files:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            data = {"status": "invalid_json"}
        results.append(
            {
                "file": path.name,
                "request_id": data.get("request_id", path.stem),
                "type": data.get("type", "unknown"),
                "status": data.get("status", "unknown"),
                "mode": data.get("mode", "unknown"),
                "dry_run": data.get("dry_run", "unknown"),
                "error": data.get("error", {}).get("reason") if isinstance(data.get("error"), dict) else None,
                "completed_at": data.get("completed_at"),
            }
        )
    return results


def request_payload(request_type: str, data: dict[str, Any]) -> dict[str, Any]:
    if request_type in REQUEST_TYPES_WITH_EMPTY_PAYLOAD:
        return {}
    if request_type == "offline_code":
        return {"code": str(data["code"])}
    if request_type in {"set_today_limit", "add_today_minutes"}:
        return {"minutes": int(data["minutes"])}
    if request_type == "set_weekly_template":
        return {"days": data["days"]}
    if request_type == "set_bedtime":
        return {
            "enabled": bool(data.get("enabled")),
            "start_min": int(data["start_min"]),
            "end_min": int(data["end_min"]),
        }
    if request_type == "set_limit_action":
        return {"action": str(data["action"])}
    if request_type == "parent_unlock_start":
        return {"duration_minutes": int(data["duration_minutes"])}
    return dict(data.get("payload", {}))


def submit_request(root: str | Path, request_type: str, data: dict[str, Any]) -> str:
    payload = request_payload(request_type, data)
    return write_request(
        Path(root),
        request_type,
        payload,
        request_id=data.get("request_id") or None,
        created_at=int(data.get("created_at") or time.time()),
    )


def submit_raw_request(root: str | Path, request_id: str, text: str) -> str:
    paths = app_paths(root)
    pending = paths.pending / f"{request_id}.json"
    tmp = pending.with_name(pending.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(pending)
    return request_id


def set_disable_flag(root: str | Path, enabled: bool) -> bool:
    paths = app_paths(root)
    flag = paths.flags / "disable.flag"
    if enabled:
        flag.write_text("", encoding="utf-8")
        return True
    if flag.exists():
        flag.unlink()
    return False


def generate_offline_code(data: dict[str, Any]) -> dict[str, Any]:
    target_date = data.get("date")
    if data.get("day_index") not in (None, ""):
        day_index = int(data["day_index"])
    else:
        day_index = day_index_for(datetime.strptime(target_date, "%Y-%m-%d").date() if target_date else today_utc8())
    nonce = int(data["nonce"]) if data.get("nonce") not in (None, "") else int(time.time() * 1000) % (MAX_NONCE + 1)
    payload = TokenPayload(
        version=TOKEN_VERSION,
        action=TOKEN_ACTION_ADD_TODAY_MINUTES,
        minutes=int(data["minutes"]),
        day_index_since_2020=day_index,
        nonce=nonce,
    )
    code = encode_token(payload, str(data["device"]), str(data["secret"]))
    return {"code": code, "day_index": day_index, "nonce": nonce}


def inspect_package(zip_path: str | Path) -> dict[str, Any]:
    path = Path(zip_path)
    if not path.is_file():
        raise FileNotFoundError(str(path))
    with zipfile.ZipFile(path) as package:
        names = package.namelist()
        has_switch = any(name.startswith("switch/") for name in names)
        has_atmosphere = any(name.startswith("atmosphere/") for name in names)
        has_boot2 = "atmosphere/contents/4200000000BD2300/flags/boot2.flag" in names
        has_exefs = "atmosphere/contents/4200000000BD2300/exefs.nsp" in names
        has_nro = any(name.startswith("switch/play-time-control/") and name.endswith(".nro") for name in names)
        config: dict[str, Any] | None = None
        if "switch/play-time-control/config.json" in names:
            config = json.loads(package.read("switch/play-time-control/config.json").decode("utf-8"))
            if "grant_secret" in config:
                config["grant_secret"] = mask_secret(config["grant_secret"])
    return {
        "zip": str(path),
        "entries": len(names),
        "has_switch": has_switch,
        "has_atmosphere": has_atmosphere,
        "has_boot2": has_boot2,
        "has_exefs": has_exefs,
        "has_nro": has_nro,
        "config": config,
    }


def install_package(zip_path: str | Path, destination: str | Path, confirm_boot2: bool) -> dict[str, Any]:
    info = inspect_package(zip_path)
    if info["has_boot2"] and not confirm_boot2:
        raise ValueError("boot2 package requires explicit confirmation")
    dest = Path(destination).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as package:
        for member in package.infolist():
            target = (dest / member.filename).resolve()
            if str(target) != str(dest) and not str(target).startswith(str(dest) + "\\") and not str(target).startswith(str(dest) + "/"):
                raise ValueError(f"unsafe package path: {member.filename}")
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with package.open(member) as src, target.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
    return {"installed_to": str(dest), "package": info}


HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Play Time Control Frontend</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 0; background: #f4f6f8; color: #16202a; }
    header { background: #16324f; color: white; padding: 14px 20px; }
    main { display: grid; grid-template-columns: 340px 1fr; gap: 16px; padding: 16px; }
    section { background: white; border: 1px solid #d8dee4; border-radius: 8px; padding: 14px; }
    label { display: block; font-size: 13px; margin: 8px 0 3px; color: #44515f; }
    input, select, textarea, button { font: inherit; box-sizing: border-box; }
    input, select, textarea { width: 100%; padding: 7px; border: 1px solid #b7c0ca; border-radius: 6px; }
    button { border: 0; border-radius: 6px; background: #1f6feb; color: white; padding: 8px 10px; margin: 6px 4px 0 0; cursor: pointer; }
    button.secondary { background: #59636e; }
    button.danger { background: #c42828; }
    pre { background: #0d1117; color: #d6deeb; padding: 12px; border-radius: 6px; overflow: auto; max-height: 420px; }
    .grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; }
    .muted { color: #687483; }
    @media (max-width: 900px) { main { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
<header><strong>Play Time Control</strong> <span class="muted">local frontend bound to 127.0.0.1</span></header>
<main>
  <section>
    <h3>SDMC root</h3>
    <label>Root path</label><input id="root" placeholder="E:\ or C:\Users\...\sdmc">
    <button onclick="refresh()">Refresh</button>
    <button class="danger" onclick="setFlag(true)">Create disable.flag</button>
    <button class="secondary" onclick="setFlag(false)">Remove disable.flag</button>

    <h3>Request</h3>
    <label>Type</label>
    <select id="rtype" onchange="fillQuickPayload()">
      <option>status</option><option>offline_code</option><option>set_today_limit</option><option>add_today_minutes</option>
      <option>disable_today_limit</option><option>block_today</option><option>restore_today_policy</option>
      <option>set_bedtime</option><option>set_limit_action</option><option>parent_unlock_start</option><option>parent_unlock_end</option>
      <option>probe_play_timer_write</option><option>probe_raw_block</option><option>probe_suspend</option>
    </select>
    <label>Payload JSON</label><textarea id="payload" rows="7">{}</textarea>
    <button onclick="submitRequest()">Submit request</button>
    <button class="secondary" onclick="quick('probe_play_timer_write', {})">Play write probe</button>

    <h3>Offline code</h3>
    <label>Device</label><input id="device" value="kid-switch">
    <label>Secret</label><input id="secret" value="replace-with-long-random-secret">
    <label>Minutes</label><input id="minutes" value="30">
    <label>Date</label><input id="date" placeholder="YYYY-MM-DD">
    <label>Nonce</label><input id="nonce" placeholder="optional">
    <button onclick="generateCode()">Generate</button>
    <button onclick="submitGenerated()">Submit generated code</button>
    <pre id="codeOut"></pre>
  </section>
  <section>
    <div class="grid">
      <section>
        <h3>Package install helper</h3>
        <label>Zip path</label><input id="zip">
        <label>Destination SDMC root</label><input id="dest">
        <label><input id="confirmBoot2" type="checkbox" style="width:auto"> confirm boot2 package</label>
        <button onclick="inspectPkg()">Inspect zip</button>
        <button class="danger" onclick="installPkg()">Install zip</button>
      </section>
      <section>
        <h3>Quick cases</h3>
        <button onclick="quick('status', {})">Disabled/Observe status</button>
        <button onclick="quick('offline_code', {code:'NOT-A-CODE'})">Bad code</button>
        <button onclick="rawUnknown()">Unknown type</button>
        <button onclick="rawBadJson()">Bad JSON</button>
        <button onclick="quick('parent_unlock_end', {})">End parent unlock</button>
      </section>
    </div>
    <h3>Output</h3>
    <pre id="out"></pre>
  </section>
</main>
<script>
let generated = null;
function rootPath() { return document.getElementById('root').value; }
async function api(path, body) {
  const res = await fetch(path, {method:'POST', headers:{'content-type':'application/json'}, body: JSON.stringify(body)});
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}
function show(data) { document.getElementById('out').textContent = JSON.stringify(data, null, 2); }
async function refresh() { show(await api('/api/state', {root: rootPath()})); }
async function setFlag(enabled) { show(await api('/api/disable-flag', {root: rootPath(), enabled})); }
function quick(type, payload) { document.getElementById('rtype').value = type; document.getElementById('payload').value = JSON.stringify(payload, null, 2); }
function fillQuickPayload() {
  const type = document.getElementById('rtype').value;
  const samples = {offline_code:{code:''}, set_today_limit:{minutes:45}, add_today_minutes:{minutes:15}, set_bedtime:{enabled:true,start_min:1260,end_min:480}, set_limit_action:{action:'remind'}, parent_unlock_start:{duration_minutes:15}};
  document.getElementById('payload').value = JSON.stringify(samples[type] || {}, null, 2);
}
async function submitRequest() {
  const type = document.getElementById('rtype').value;
  const payload = JSON.parse(document.getElementById('payload').value || '{}');
  show(await api('/api/request', {root: rootPath(), type, ...payload}));
}
async function generateCode() {
  generated = await api('/api/token', {device:device.value, secret:secret.value, minutes:minutes.value, date:date.value, nonce:nonce.value});
  codeOut.textContent = JSON.stringify(generated, null, 2);
}
async function submitGenerated() {
  if (!generated) await generateCode();
  quick('offline_code', {code: generated.code});
  await submitRequest();
}
async function rawUnknown() {
  const id = 'stage-e-unknown-' + Date.now();
  show(await api('/api/raw-request', {root: rootPath(), request_id:id, text:JSON.stringify({version:1,request_id:id,type:'stage_e_unknown',created_at:Math.floor(Date.now()/1000),payload:{}})}));
}
async function rawBadJson() {
  const id = 'stage-e-bad-json-' + Date.now();
  show(await api('/api/raw-request', {root: rootPath(), request_id:id, text:'{"version":1,"request_id":"' + id + '","type":"offline_code","created_at":1783526400,"payload":{"code":"NOT-A-CODE"}'}));
}
async function inspectPkg() { show(await api('/api/package/inspect', {zip: zip.value})); }
async function installPkg() { show(await api('/api/package/install', {zip: zip.value, destination: dest.value || rootPath(), confirm_boot2: confirmBoot2.checked})); }
fillQuickPayload();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def _send(self, status: int, content_type: str, data: bytes) -> None:
        self.send_response(status)
        self.send_header("content-type", content_type)
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status: int, data: dict[str, Any]) -> None:
        self._send(status, "application/json; charset=utf-8", json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8"))

    def _body(self) -> dict[str, Any]:
        length = int(self.headers.get("content-length", "0"))
        if length <= 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self._send(200, "text/html; charset=utf-8", HTML.encode("utf-8"))
            return
        if parsed.path == "/api/state":
            qs = parse_qs(parsed.query)
            root = qs.get("root", [""])[0]
            self._json(200, layout_health(root))
            return
        self._json(404, {"error": "not_found"})

    def do_POST(self) -> None:
        try:
            body = self._body()
            if self.path == "/api/state":
                self._json(200, layout_health(body["root"]))
            elif self.path == "/api/request":
                request_id = submit_request(body["root"], body["type"], body)
                self._json(200, {"request_id": request_id, "state": layout_health(body["root"])})
            elif self.path == "/api/raw-request":
                request_id = submit_raw_request(body["root"], body["request_id"], body["text"])
                self._json(200, {"request_id": request_id})
            elif self.path == "/api/disable-flag":
                enabled = set_disable_flag(body["root"], bool(body.get("enabled")))
                self._json(200, {"disable_flag": enabled, "state": layout_health(body["root"])})
            elif self.path == "/api/token":
                self._json(200, generate_offline_code(body))
            elif self.path == "/api/package/inspect":
                self._json(200, inspect_package(body["zip"]))
            elif self.path == "/api/package/install":
                self._json(200, install_package(body["zip"], body["destination"], bool(body.get("confirm_boot2"))))
            else:
                self._json(404, {"error": "not_found"})
        except Exception as exc:
            self._json(400, {"error": str(exc)})

    def log_message(self, format: str, *args: Any) -> None:
        sys.stderr.write("ptc-front: " + (format % args) + "\n")


def serve(host: str, port: int) -> None:
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"http://{host}:{server.server_port}")
    server.serve_forever()


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the local Play Time Control frontend.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    serve(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
