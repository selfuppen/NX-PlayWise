#!/usr/bin/env python3
from __future__ import annotations

from datetime import date
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import sys
import threading
from urllib.error import HTTPError
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from grant_code import today_utc8  # noqa: E402
from ptc_frontend_server import HTML, Handler, generate_offline_code  # noqa: E402


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_true(value, label: str) -> None:
    if not value:
        raise AssertionError(f"{label}: expected truthy value")


def expect_value_error(data: dict, expected_text: str, label: str) -> None:
    try:
        generate_offline_code(data)
    except ValueError as exc:
        assert_true(expected_text in str(exc), label)
    else:
        raise AssertionError(f"{label}: expected ValueError")


def test_token_generation() -> None:
    short_token = generate_offline_code(
        {
            "device": "test-device",
            "secret": "test-secret",
            "tier_minutes": 30,
            "date": "2026-07-08",
            "nonce": 7,
        }
    )
    assert_equal(short_token["code"], "10514680", "generated v2 code")
    assert_equal(short_token["date"], "2026-07-08", "generated date")
    assert_equal(short_token["day_index"], 2380, "generated day")
    assert_equal(short_token["minutes"], 30, "generated tier")
    assert_equal(short_token["token_version"], 2, "generated token version")

    default_date = generate_offline_code(
        {
            "device": "test-device",
            "secret": "test-secret",
            "tier_minutes": 5,
            "nonce": 0,
        }
    )
    assert_equal(default_date["date"], today_utc8().isoformat(), "default UTC+8 date")

    expect_value_error({"secret": "x", "tier_minutes": 30}, "device", "missing device")
    expect_value_error({"device": "x", "tier_minutes": 30}, "secret", "missing secret")
    expect_value_error(
        {"device": "x", "secret": "y", "tier_minutes": 7, "date": date.today().isoformat(), "nonce": 0},
        "multiple of 5",
        "invalid tier",
    )


def request_json(url: str, payload: dict) -> tuple[int, dict]:
    request = Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"content-type": "application/json"},
        method="POST",
    )
    try:
        with urlopen(request) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        return exc.code, json.loads(exc.read().decode("utf-8"))


def test_page_and_api_surface() -> None:
    assert_true('type="date"' in HTML, "date picker is present")
    assert_true("todayUtc8()" in HTML, "date defaults to UTC+8 today")
    assert_true("生成 8 位数字加时码" in HTML, "v2 copy is present")
    assert_true("Package install helper" not in HTML, "package controls removed")
    assert_true("Payload JSON" not in HTML, "request controls removed")
    assert_true("V2 tier minutes" not in HTML and ">Minutes<" not in HTML, "legacy minutes fields removed")
    assert_true("/api/state" not in HTML and "/api/request" not in HTML, "legacy API calls removed")

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base_url = f"http://127.0.0.1:{server.server_port}"
    try:
        with urlopen(base_url + "/") as response:
            page = response.read().decode("utf-8")
            assert_equal(response.status, 200, "frontend status")
            assert_true("Offline code" in page, "frontend body")

        status, token = request_json(
            base_url + "/api/token",
            {
                "device": "test-device",
                "secret": "test-secret",
                "tier_minutes": 30,
                "date": "2026-07-08",
                "nonce": 7,
            },
        )
        assert_equal(status, 200, "token API status")
        assert_equal(token["code"], "10514680", "token API code")

        status, body = request_json(base_url + "/api/state", {})
        assert_equal(status, 404, "removed API status")
        assert_equal(body["error"], "not_found", "removed API body")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def main() -> int:
    test_token_generation()
    test_page_and_api_surface()
    print("PTC frontend server tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
