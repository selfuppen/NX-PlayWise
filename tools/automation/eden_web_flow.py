"""X02: a real browser imports NRO-exported pairing and issues a redeemed code."""
from __future__ import annotations

from datetime import date, timedelta
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import re
import shutil
import threading
import time
from urllib.parse import urlsplit
import uuid

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import decode, digest, read, require, save
from tools.lib.ptc_token_v2 import encode_token, tier_for_minutes
from tools.ptc_frontend_server import Handler, STATIC_ASSETS

WEB_SCENARIOS = ('setup', 'web_pair_export', 'web_issue', 'web_redeem', 'web_reopen')


def validate_browser_result(value: dict, request: dict, pairing: dict) -> None:
    require(set(value) == {'schema_version', 'run_id', 'status', 'browser', 'device_id', 'date',
                          'day_index', 'minutes', 'nonce', 'code', 'import_confirmed',
                          'pairing_persisted_after_reload', 'nonce_persisted_after_reload', 'external_requests',
                          'screenshot_sha256'}, 'browser result schema differs')
    require(type(value['schema_version']) is int and value['schema_version'] == 1 and value['run_id'] == request['run_id'] and
            value['status'] == 'pass' and isinstance(value['browser'], str) and value['browser'], 'browser result identity differs')
    require(value['device_id'] == pairing['device_id'] == request['device_id'] and value['date'] == request['date'] and
            type(value['day_index']) is int and value['day_index'] == request['day_index'] and
            type(value['minutes']) is int and value['minutes'] == 30 and type(value['nonce']) is int and
            0 <= value['nonce'] <= 511 and isinstance(value['code'], str) and re.fullmatch(r'\d{8}', value['code']),
            'browser-issued device/day/amount/nonce/code differs')
    require(all(value[k] is True for k in ('import_confirmed', 'pairing_persisted_after_reload', 'nonce_persisted_after_reload')) and
            value['external_requests'] == [] and isinstance(value['screenshot_sha256'], str) and
            re.fullmatch('[0-9a-f]{64}', value['screenshot_sha256']) is not None,
            'browser import/storage/network evidence differs')
    # Only verify browser output here; the code entered on NRO must come from the DOM/browser result.
    require(value['code'] == encode_token(tier_for_minutes(30), value['nonce'], pairing['device_id'],
                                         pairing['grant_secret'], request['day_index']), 'browser code binding differs')


class BrowserBridge:
    """One bounded loopback handoff; no app endpoints, business writes or retries."""
    def __init__(self, output: Path, pairing: Path, day_index: int, device: str) -> None:
        self.output, self.pairing = output, pairing
        self.done = threading.Event()
        self.lock = threading.Lock()
        self.result = None
        self.run_id = uuid.uuid4().hex
        bridge = self

        class BrowserHandler(Handler):
            def log_message(self, *_: object) -> None:
                pass  # Do not log bodies, pairing credentials or generated codes.

            def _serve_asset(self, *, include_body: bool) -> None:
                asset = STATIC_ASSETS.get(urlsplit(self.path).path)
                if asset is None:
                    self._send_headers(404, 'text/plain', 0)
                    return
                filename, content_type = asset
                target = bridge.output / 'artifacts/frontend' / filename
                if not target.is_file():
                    self._send_headers(404, 'text/plain', 0)
                    return
                body = target.read_bytes()
                self._send_headers(200, content_type, len(body))
                if include_body:
                    self.wfile.write(body)

            def do_GET(self) -> None:
                path = urlsplit(self.path).path
                if path in ('/__eden_test/request', '/__eden_test/parent-import.json'):
                    body = (json.dumps(bridge.request).encode() if path.endswith('/request') else bridge.pairing.read_bytes())
                    self._send_headers(200, 'application/json', len(body))
                    self.wfile.write(body)
                else:
                    super().do_GET()

            def do_POST(self) -> None:
                try:
                    length = int(self.headers.get('content-length', '-1'))
                    path = urlsplit(self.path).path
                    require(0 <= length <= (8 * 1024 * 1024 if path == '/__eden_test/screenshot' else 16384), 'body limit')
                    body = self.rfile.read(length)
                    require(len(body) == length and self.headers.get('X-PlayWise-Run') == bridge.run_id, 'handoff binding')
                    with bridge.lock:
                        require(not bridge.done.is_set(), 'result already completed')
                        if path == '/__eden_test/screenshot':
                            require(body.startswith(b'\x89PNG\r\n\x1a\n'), 'not a PNG')
                            target = bridge.output / 'browser/generated.png'
                            target.parent.mkdir(exist_ok=True)
                            with target.open('xb') as output:
                                output.write(body)
                        elif path == '/__eden_test/result':
                            value = decode(body.decode('utf-8'))
                            validate_browser_result(value, bridge.request, read(bridge.pairing))
                            require(digest(bridge.output / 'browser/generated.png') == value['screenshot_sha256'], 'screenshot binding')
                            save(bridge.output / 'browser/result.json', value)
                            bridge.result = value
                            bridge.done.set()
                        else:
                            raise ValueError('unknown handoff path')
                    self._send_headers(200, 'application/json', 2)
                    self.wfile.write(b'{}')
                except (OSError, ValueError, RuntimeError) as exc:
                    self._send_headers(409, 'text/plain', 8)
                    self.wfile.write(b'rejected')

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), BrowserHandler)
        self.server.daemon_threads = True
        origin = f'http://127.0.0.1:{self.server.server_port}'
        self.request = {'schema_version': 1, 'run_id': self.run_id, 'origin': origin, 'device_id': device,
                        'day_index': day_index, 'date': (date(2020, 1, 1) + timedelta(days=day_index)).isoformat(),
                        'minutes': 30, 'parent_import_sha256': digest(pairing),
                        'driver_sha256': digest(output / 'artifacts/eden_web_browser.js')}
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        save(self.output / 'browser-request.json', self.request)
        print(json.dumps({'phase': 'browser_required', 'request': str(self.output / 'browser-request.json'),
                          'origin': self.request['origin'], 'timeout_seconds': 180}), flush=True)
        return self

    def __exit__(self, *_: object) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)


class WebFlow(MainFlow):
    def web_pair_export(self) -> None:
        self.page(2)
        export = self.app / 'parent-import.json'
        require(not export.exists(), 'pairing export must be created by this UI journey')
        self.action(target='card_1')
        self.wait_ui(overlay=22, pin_length=0)
        self.enter_pin('110')
        self.wait_ui(overlay=7, waiting=False)
        # QR rendering is not a camera/optical decoding qualification.
        self.action(button='A')
        self.wait_ui(overlay=38, waiting=False)
        pair = read(export)
        require(pair == {'version': 1, 'device_id': 'eden-switch',
                         'grant_secret': 'playwise-eden-test-secret-00000001'}, 'not the isolated Eden fixture export')
        target = self.output / 'browser/input/parent-import.json'
        target.parent.mkdir(parents=True)
        shutil.copy2(export, target)
        self.checked('web_pair_export', ['normal parent PIN opens pairing page', 'UI export creates fresh matching fixture'],
                     export_sha256=digest(export))

    def web_issue(self) -> None:
        self.action(button='B')
        self.wait_ui(overlay=7)
        self.action(button='B')
        self.wait_ui(view=1, overlay=0)
        # Obtain a new read-only receipt after the export, never a cached host-selected date.
        latest = self.refresh()
        pairing = self.output / 'browser/input/parent-import.json'
        with BrowserBridge(self.output, pairing, latest['state']['day_index'], read(pairing)['device_id']) as bridge:
            require(bridge.done.wait(timeout=min(180, max(0, self.deadline - time.monotonic()))),
                    'browser handoff timed out; no NRO code input or retry')
            self.web_code = bridge.result
        self.checked('web_issue', ['real browser UI confirms exported device', 'reload retains pairing and generated nonce',
                                   'DOM-generated code bound to guest date/device/amount'], browser_result_sha256=digest(self.output / 'browser/result.json'))

    def web_redeem(self) -> None:
        self.wait_ui(view=1, overlay=0)
        self.action(button='B')
        self.wait_ui(view=0, overlay=0)
        ledger = self.app / 'ledger/used_nonces.jsonl'
        require(not ledger.exists() or not ledger.read_bytes(), 'fresh web journey already has used nonce')
        before = self.type_code(self.web_code['code'])
        preview = self.wait_receipt('preview_offline_code', before)
        require(preview['status'] == 'ok' and preview['preview']['effective_add_minutes'] == 30, 'web code preview differs')
        self.wait_ui(overlay=3, waiting=False)
        before = self.ids()
        self.action(button='A')
        grant = self.wait_receipt('offline_code', before)
        require(grant['status'] == 'ok', 'web-issued redemption rejected')
        self.wait_ui(overlay=12, code_result_failed=False, code_result_pending=False)
        entries = [decode(line) for line in ledger.read_text(encoding='utf-8').splitlines()]
        require(len(entries) == 1 and entries[0]['nonce'] == self.web_code['nonce'] and
                entries[0]['day_index'] == self.web_code['day_index'] and entries[0]['token_version'] == 2,
                'browser nonce not consumed exactly once')
        rules = read(self.app / 'rules.json')
        require(rules['today_override_present'] is True and rules['today_override_day_index'] == self.web_code['day_index'] and
                rules['today_override_minutes'] == 150, 'web grant did not add 30 to 120')
        self.checked('web_redeem', ['web DOM code accepted by normal NRO entry and confirmation',
                                   'one successful transaction consumes browser nonce and changes allowance to 150'],
                     grant=grant, preview=preview, ledger_sha256=digest(ledger))

    def web_reopen(self) -> None:
        self.action(button='B')
        self.wait_ui(view=0, overlay=0)
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0, today_minutes=150)
        require(digest(self.app / 'ledger/used_nonces.jsonl') == read(self.output / 'assertions/web_redeem.json')['ledger_sha256'],
                'reopen lost browser nonce')
        self.checked('web_reopen', ['same lightweight profile preserves allowance and consumed browser nonce'], today_minutes=150)


def verify_web(output: Path) -> None:
    request = read(output / 'browser-request.json')
    pairing = read(output / 'browser/input/parent-import.json')
    browser = read(output / 'browser/result.json')
    validate_browser_result(browser, request, pairing)
    require(digest(output / 'browser/generated.png') == browser['screenshot_sha256'] and
            digest(output / 'artifacts/eden_web_browser.js') == request['driver_sha256'] and
            digest(output / 'browser/input/parent-import.json') == request['parent_import_sha256'], 'web evidence hash differs')
    for name in WEB_SCENARIOS[1:]:
        item = read(output / f'assertions/{name}.json')
        require(item['passed'] is True and bool(item['checks']), 'missing web journey assertion')
    grant = read(output / 'assertions/web_redeem.json')
    require(grant['grant']['status'] == 'ok' and grant['grant']['type'] == 'offline_code' and
            grant['preview']['status'] == 'ok' and grant['preview']['preview']['effective_add_minutes'] == 30,
            'web preview/receipt differs')
    rows = [read(p) for p in (output / 'receipts').glob('*.json')]
    require(sum(r['type'] == 'offline_code' for r in rows) == 1 and grant['grant'] in rows and grant['preview'] in rows,
            'web transaction missing or duplicated')
    rules = read(output / 'app-final/rules.json')
    require(rules['today_override_minutes'] == 150 and rules['today_override_day_index'] == browser['day_index'], 'web persisted rules differ')
    ledger = output / 'app-final/ledger/used_nonces.jsonl'
    require(digest(ledger) == grant['ledger_sha256'], 'web ledger changed across reopen')
    entries = [decode(line) for line in ledger.read_text(encoding='utf-8').splitlines()]
    require(len(entries) == 1 and entries[0]['nonce'] == browser['nonce'] and entries[0]['day_index'] == browser['day_index'],
            'persisted nonce differs from browser')
    activity = [decode(line) for line in (output / 'app-final/activity/history.jsonl').read_text(encoding='utf-8').splitlines()]
    require(sum(r['action'] == 'offline_grant' and r['minutes'] == 30 and r['effective_minutes'] == 30 for r in activity) == 1,
            'web activity missing or duplicated')
    ui = read(output / 'checkpoints/web_reopen.json')['ui']
    require(ui['view'] == 0 and ui['today_minutes'] == 150, 'web reopen UI differs')
