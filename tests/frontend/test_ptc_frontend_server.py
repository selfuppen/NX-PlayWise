#!/usr/bin/env python3
from __future__ import annotations

from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import sys
import threading
from urllib.error import HTTPError
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
STATIC_ROOT = TOOLS / "ptc_frontend"
sys.path.insert(0, str(TOOLS))

from ptc_frontend_server import Handler, STATIC_ASSETS  # noqa: E402


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_true(value, label: str) -> None:
    if not value:
        raise AssertionError(f"{label}: expected truthy value")


def request(url: str, *, method: str = "GET") -> tuple[int, dict[str, str], bytes]:
    req = Request(url, data=b"{}" if method == "POST" else None, method=method)
    try:
        with urlopen(req) as response:
            return response.status, {key.lower(): value for key, value in response.headers.items()}, response.read()
    except HTTPError as exc:
        return exc.code, {key.lower(): value for key, value in exc.headers.items()}, exc.read()


def test_static_assets_and_copy() -> None:
    index = (STATIC_ROOT / "index.html").read_text(encoding="utf-8")
    app = (STATIC_ROOT / "app.js").read_text(encoding="utf-8")
    token = (STATIC_ROOT / "token.js").read_text(encoding="utf-8")
    storage = (STATIC_ROOT / "storage.js").read_text(encoding="utf-8")
    pairing = (STATIC_ROOT / "pairing.js").read_text(encoding="utf-8")
    worker = (STATIC_ROOT / "sw.js").read_text(encoding="utf-8")
    manifest = json.loads((STATIC_ROOT / "manifest.webmanifest").read_text(encoding="utf-8"))

    for path, (filename, _) in STATIC_ASSETS.items():
        assert_true((STATIC_ROOT / filename).is_file(), f"asset for {path}")
    assert_true("/api/token" not in index + app + worker, "removed token API is not referenced")
    assert_true("https://" not in index + app + token + storage + pairing + worker, "no external HTTPS dependency")
    assert_true("http://" not in index + app + token + storage + pairing + worker, "no external HTTP dependency")
    assert_true("playwise-public-demo-secret-0001" in index, "public demo secret is explicit")
    assert_true("device_id" in app + pairing and "grant_secret" in app + pairing, "fragment pairing fields")
    assert_true("history.replaceState" in app, "fragment is cleared after parsing")
    assert_true("importFile" in index + app, "configuration file import")
    assert_true("生成 8 位数字加时码" in index, "v2 generation copy")
    assert_true("任你玩" in index and "PlayWise" in index, "bilingual product name")
    assert_true("Play Wise. Play More." in index, "brand slogan")
    assert_true("used_token" not in index and "加时码已使用" in index, "collision recovery copy")
    assert_true("cryptoApi.subtle" in token, "Web Crypto HMAC implementation")
    assert_true("getRandomValues" in token and "Math.random" not in token, "cryptographic random nonce")
    assert_true("10514680" in token, "fixed vector self-test")
    assert_true("ptc.frontend.config.v1" in storage, "versioned config storage key")
    assert_true("ptc.frontend.nonces.v1" in storage, "versioned nonce storage key")
    assert_true("navigator.serviceWorker.register" in app, "service worker registration")
    assert_true("caches.open" in worker and "localStorage" not in worker, "worker caches only assets")
    assert_equal(manifest["display"], "standalone", "PWA display mode")
    assert_equal(manifest["start_url"], "./", "portable PWA start URL")
    assert_equal(manifest["name"], "任你玩 · PlayWise", "PWA product name")
    assert_true("STATIC_URLS.has(url.href)" in worker, "worker caches only the static allowlist")
    assert_true(any("maskable" in icon["purpose"] for icon in manifest["icons"]), "maskable icon")


def test_http_surface() -> None:
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base_url = f"http://127.0.0.1:{server.server_port}"
    try:
        for path, (_, expected_type) in STATIC_ASSETS.items():
            status, headers, body = request(base_url + path)
            assert_equal(status, 200, f"GET {path}")
            assert_true(body, f"body for {path}")
            assert_equal(headers["content-type"], expected_type, f"MIME for {path}")
            assert_equal(headers["x-content-type-options"], "nosniff", f"nosniff for {path}")
            assert_equal(headers["referrer-policy"], "no-referrer", f"referrer policy for {path}")
            assert_true("default-src 'self'" in headers["content-security-policy"], f"CSP for {path}")

        status, _, body = request(base_url + "/styles.css", method="HEAD")
        assert_equal(status, 200, "HEAD asset status")
        assert_equal(body, b"", "HEAD has no body")

        for path in ["/missing", "/../grant_code.py", "/%2e%2e/grant_code.py", "/api/token"]:
            status, _, _ = request(base_url + path)
            assert_equal(status, 404, f"GET rejects {path}")

        status, _, _ = request(base_url + "/api/token", method="POST")
        assert_equal(status, 404, "removed POST token API")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def test_javascript_modules() -> None:
    node = shutil.which("node")
    if node is None:
        return
    runner = ROOT / "tests" / "frontend" / "test_ptc_frontend_modules.mjs"
    result = subprocess.run([node, str(runner)], cwd=ROOT, text=True, capture_output=True)
    if result.returncode != 0:
        raise AssertionError(f"JavaScript frontend tests failed:\n{result.stdout}\n{result.stderr}")
    assert_true("PTC frontend JavaScript tests passed" in result.stdout, "JavaScript test completion")


def main() -> int:
    test_static_assets_and_copy()
    test_http_surface()
    test_javascript_modules()
    print("PTC static frontend tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
