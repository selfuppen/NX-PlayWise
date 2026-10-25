from __future__ import annotations

from copy import deepcopy
import hashlib
import http.client
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.automation.eden_runner import GateError, scenario_names
from tools.automation.eden_web_flow import BrowserBridge, WEB_SCENARIOS, validate_browser_result
from tools.lib.ptc_token_v2 import encode_token, tier_for_minutes


def payload(request: dict, pair: dict, png: bytes) -> dict:
    return {'schema_version': 1, 'run_id': request['run_id'], 'status': 'pass', 'browser': 'Host fixture',
            'device_id': pair['device_id'], 'date': request['date'], 'day_index': request['day_index'],
            'minutes': 30, 'nonce': 17, 'code': encode_token(tier_for_minutes(30), 17, pair['device_id'], pair['grant_secret'], request['day_index']),
            'import_confirmed': True, 'pairing_persisted_after_reload': True, 'nonce_persisted_after_reload': True,
            'external_requests': [], 'screenshot_sha256': hashlib.sha256(png).hexdigest()}


def main() -> None:
    assert scenario_names('web') == WEB_SCENARIOS and len(WEB_SCENARIOS) == 5
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        pair = {'version': 1, 'device_id': 'eden-switch', 'grant_secret': 'playwise-eden-test-secret-00000001'}
        pairing = root / 'parent-import.json'
        pairing.write_text(json.dumps(pair), encoding='utf-8')
        (root / 'artifacts').mkdir()
        (root / 'artifacts/eden_web_browser.js').write_text('// fixture', encoding='utf-8')
        with BrowserBridge(root, pairing, 2489, pair['device_id']) as bridge:
            png = b'\x89PNG\r\n\x1a\nhost-fixture'
            good = payload(bridge.request, pair, png)
            validate_browser_result(good, bridge.request, pair)
            for key, bad in [('day_index', True), ('run_id', 'other'), ('minutes', 5), ('nonce', 512),
                             ('code', '00000000'), ('import_confirmed', 1), ('external_requests', ['https://outside']),
                             ('screenshot_sha256', None)]:
                try:
                    validate_browser_result({**good, key: bad}, bridge.request, pair)
                except GateError:
                    pass
                else:
                    raise AssertionError(f'invalid browser binding accepted: {key}')
            conn = http.client.HTTPConnection('127.0.0.1', bridge.server.server_port, timeout=5)
            def post(path: str, data: bytes, run: str) -> int:
                conn.request('POST', path, body=data, headers={'X-PlayWise-Run': run})
                reply = conn.getresponse()
                reply.read()
                return reply.status
            assert post('/__eden_test/screenshot', png, 'wrong') == 409
            assert not (root / 'browser/generated.png').exists() and not bridge.done.is_set()
            assert post('/__eden_test/screenshot', png, bridge.run_id) == 200
            assert post('/__eden_test/result', json.dumps({**good, 'date': '2020-01-01'}).encode(), bridge.run_id) == 409
            assert not bridge.done.is_set()
            assert post('/__eden_test/result', json.dumps(good).encode(), bridge.run_id) == 200
            assert bridge.done.is_set() and bridge.result == good
            assert post('/__eden_test/result', json.dumps(good).encode(), bridge.run_id) == 409
            assert json.loads((root / 'browser/result.json').read_text()) == good
            conn.close()
    print('PASS: X02 browser bridge, date/device/nonce binding, bounded handoff and duplicate rejection')


if __name__ == '__main__':
    main()
