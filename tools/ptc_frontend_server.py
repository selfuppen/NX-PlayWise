#!/usr/bin/env python3
from __future__ import annotations

from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
from pathlib import Path
import sys
from typing import Any
from urllib.parse import urlparse

from grant_code import day_index_for, next_v2_nonce, today_utc8
from ptc_token_v2 import MAX_NONCE as V2_MAX_NONCE, encode_token as encode_token_v2, tier_for_minutes


def generate_offline_code(data: dict[str, Any]) -> dict[str, Any]:
    device = str(data.get("device", ""))
    secret = str(data.get("secret", ""))
    if not device:
        raise ValueError("device is required")
    if not secret:
        raise ValueError("secret is required")

    target_date_text = str(data.get("date") or "")
    target_date = datetime.strptime(target_date_text, "%Y-%m-%d").date() if target_date_text else today_utc8()
    day_index = day_index_for(target_date)
    tier_minutes = int(data.get("tier_minutes") or 30)
    tier_index = tier_for_minutes(tier_minutes)
    nonce = int(data["nonce"]) if data.get("nonce") not in (None, "") else next_v2_nonce(
        Path(data["v2_nonce_state"]) if data.get("v2_nonce_state") else Path.home() / ".ptc" / "token_v2_nonce_state.json",
        device,
        day_index,
    )
    if not 0 <= nonce <= V2_MAX_NONCE:
        raise ValueError("v2 nonce must be in range 0..511")
    return {
        "code": encode_token_v2(tier_index, nonce, device, secret, day_index),
        "date": target_date.isoformat(),
        "day_index": day_index,
        "nonce": nonce,
        "token_version": 2,
        "tier_index": tier_index,
        "minutes": tier_minutes,
    }


HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Offline code</title>
  <style>
    :root { color-scheme: light; }
    * { box-sizing: border-box; }
    body { margin: 0; background: #f4f6f8; color: #20262e; font-family: system-ui, sans-serif; }
    header { border-bottom: 1px solid #d8dee6; background: #ffffff; padding: 20px 24px; }
    header strong { font-size: 22px; }
    main { width: min(100% - 32px, 720px); margin: 44px auto; }
    .tool { border: 1px solid #d8dee6; border-radius: 8px; background: #ffffff; padding: 28px; }
    h1 { margin: 0 0 26px; font-size: 28px; letter-spacing: 0; }
    .fields { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px 20px; }
    label { display: block; color: #56616f; font-size: 14px; }
    input, select, button { width: 100%; min-height: 44px; margin-top: 7px; font: inherit; }
    input, select { border: 1px solid #aeb8c4; border-radius: 6px; background: #ffffff; padding: 9px 11px; color: #20262e; }
    input:focus, select:focus { outline: 3px solid #bfdbfe; border-color: #1d6fa5; }
    button { border: 0; border-radius: 6px; cursor: pointer; font-weight: 650; }
    .primary { margin-top: 24px; background: #1d6fa5; color: #ffffff; }
    .primary:disabled { cursor: wait; background: #77838f; }
    .result { margin-top: 24px; border-top: 1px solid #d8dee6; padding-top: 24px; }
    .result[hidden] { display: none; }
    .code { margin: 0; color: #146c43; font-family: ui-monospace, monospace; font-size: 38px; font-weight: 750; letter-spacing: 0; text-align: center; }
    .meta { margin: 10px 0 0; color: #687483; text-align: center; }
    .copy { margin-top: 18px; border: 1px solid #aeb8c4; background: #ffffff; color: #26313d; }
    .error { margin-top: 20px; border-left: 5px solid #b4232c; background: #fff1f2; color: #8f1d27; padding: 14px 16px; }
    .error[hidden] { display: none; }
    @media (max-width: 620px) {
      main { margin: 24px auto; }
      .tool { padding: 20px; }
      .fields { grid-template-columns: 1fr; }
      .code { font-size: 32px; }
    }
  </style>
</head>
<body>
<header><strong>Play Time Control</strong></header>
<main>
  <section class="tool" aria-labelledby="page-title">
    <h1 id="page-title">Offline code</h1>
    <form id="generator">
      <div class="fields">
        <label>设备<input id="device" name="device" value="kid-switch" autocomplete="off" required></label>
        <label>密钥<input id="secret" name="secret" type="password" value="replace-with-long-random-secret" autocomplete="off" required></label>
        <label>日期<input id="date" name="date" type="date" required></label>
        <label>加时时长
          <select id="tierMinutes" name="tier_minutes">
            <option value="5">5 分钟</option><option value="10">10 分钟</option><option value="15">15 分钟</option>
            <option value="20">20 分钟</option><option value="25">25 分钟</option><option value="30" selected>30 分钟</option>
            <option value="35">35 分钟</option><option value="40">40 分钟</option><option value="45">45 分钟</option>
            <option value="50">50 分钟</option><option value="55">55 分钟</option><option value="60">60 分钟</option>
            <option value="65">65 分钟</option><option value="70">70 分钟</option><option value="75">75 分钟</option>
            <option value="80">80 分钟</option><option value="85">85 分钟</option><option value="90">90 分钟</option>
            <option value="95">95 分钟</option><option value="100">100 分钟</option><option value="105">105 分钟</option>
            <option value="110">110 分钟</option><option value="115">115 分钟</option><option value="120">120 分钟</option>
          </select>
        </label>
      </div>
      <button id="generate" class="primary" type="submit">生成 8 位数字加时码</button>
    </form>
    <div id="error" class="error" role="alert" hidden></div>
    <section id="result" class="result" aria-live="polite" hidden>
      <p id="code" class="code"></p>
      <p id="meta" class="meta"></p>
      <button id="copy" class="copy" type="button">复制加时码</button>
    </section>
  </section>
</main>
<script>
const form = document.getElementById('generator');
const dateInput = document.getElementById('date');
const generateButton = document.getElementById('generate');
const result = document.getElementById('result');
const codeOutput = document.getElementById('code');
const metaOutput = document.getElementById('meta');
const errorOutput = document.getElementById('error');

function todayUtc8() {
  return new Date(Date.now() + 8 * 60 * 60 * 1000).toISOString().slice(0, 10);
}

dateInput.value = todayUtc8();

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  generateButton.disabled = true;
  errorOutput.hidden = true;
  result.hidden = true;
  try {
    const response = await fetch('/api/token', {
      method: 'POST',
      headers: {'content-type': 'application/json'},
      body: JSON.stringify({
        device: form.device.value,
        secret: form.secret.value,
        date: form.date.value,
        tier_minutes: form.tier_minutes.value
      })
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || response.statusText);
    codeOutput.textContent = data.code;
    metaOutput.textContent = `${data.date} · ${data.minutes} 分钟 · v${data.token_version}`;
    result.hidden = false;
  } catch (error) {
    errorOutput.textContent = error instanceof Error ? error.message : String(error);
    errorOutput.hidden = false;
  } finally {
    generateButton.disabled = false;
  }
});

document.getElementById('copy').addEventListener('click', async () => {
  await navigator.clipboard.writeText(codeOutput.textContent || '');
});
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
        self._send(status, "application/json; charset=utf-8", json.dumps(data, ensure_ascii=False).encode("utf-8"))

    def _body(self) -> dict[str, Any]:
        length = int(self.headers.get("content-length", "0"))
        if length <= 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def do_GET(self) -> None:
        if urlparse(self.path).path == "/":
            self._send(200, "text/html; charset=utf-8", HTML.encode("utf-8"))
            return
        self._json(404, {"error": "not_found"})

    def do_POST(self) -> None:
        try:
            if urlparse(self.path).path != "/api/token":
                self._json(404, {"error": "not_found"})
                return
            self._json(200, generate_offline_code(self._body()))
        except Exception as exc:
            self._json(400, {"error": str(exc)})

    def log_message(self, format: str, *args: Any) -> None:
        sys.stderr.write("ptc-front: " + (format % args) + "\n")


def serve(host: str, port: int) -> None:
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"http://{host}:{server.server_port}")
    server.serve_forever()


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the Offline code generator frontend.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    serve(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
