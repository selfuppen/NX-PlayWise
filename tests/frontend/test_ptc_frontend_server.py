#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from package_sdmc import create_package, write_zip  # noqa: E402
from ptc_frontend_server import (  # noqa: E402
    generate_offline_code,
    inspect_package,
    install_package,
    layout_health,
    set_disable_flag,
    submit_raw_request,
    submit_request,
)


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_true(value, label: str) -> None:
    if not value:
        raise AssertionError(f"{label}: expected truthy value")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_requests_and_flag() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-frontend-") as tmp_dir:
        root = Path(tmp_dir)
        request_id = submit_request(root, "set_today_limit", {"minutes": 45, "request_id": "web-set-today"})
        assert_equal(request_id, "web-set-today", "request id")
        request = read_json(root / "switch/play-time-control/inbox/pending/web-set-today.json")
        assert_equal(request["type"], "set_today_limit", "request type")
        assert_equal(request["payload"]["minutes"], 45, "request minutes")

        raw_id = submit_raw_request(root, "web-unknown", '{"version":1,"request_id":"web-unknown","type":"stage_e_unknown","created_at":1,"payload":{}}')
        assert_equal(raw_id, "web-unknown", "raw id")
        assert_true((root / "switch/play-time-control/inbox/pending/web-unknown.json").is_file(), "raw request file")

        assert_true(set_disable_flag(root, True), "set disable flag")
        assert_true((root / "switch/play-time-control/flags/disable.flag").is_file(), "disable flag exists")
        assert_true(not set_disable_flag(root, False), "clear disable flag return")
        assert_true(not (root / "switch/play-time-control/flags/disable.flag").exists(), "disable flag removed")

        health = layout_health(root)
        assert_equal(health["counts"]["pending"], 2, "pending count")
        assert_true("config.json" in " ".join(health["missing"]), "health reports missing config")


def test_token_generation_and_package_install() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-frontend-package-") as tmp_dir:
        tmp = Path(tmp_dir)
        token = generate_offline_code(
            {
                "device": "test-device",
                "secret": "test-secret",
                "minutes": 30,
                "day_index": 2380,
                "nonce": 4660,
            }
        )
        assert_equal(token["code"], "241W-2AC0-04HM-7YW5", "generated code")
        assert_equal(token["day_index"], 2380, "generated day")
        short_token = generate_offline_code(
            {
                "device": "test-device",
                "secret": "test-secret",
                "tier_minutes": 30,
                "day_index": 2380,
                "nonce": 7,
            }
        )
        assert_equal(short_token["code"], "10514680", "generated v2 short code")
        assert_equal(short_token["token_version"], 2, "generated v2 version")

        package_root = tmp / "pkg-root"
        zip_path = tmp / "observe.zip"
        create_package(
            package_root,
            "observe",
            include_boot2=False,
            device_id="kid-switch",
            grant_secret="replace-with-long-random-secret",
            max_add_minutes=120,
            nro=None,
            sysmodule_exefs=None,
            toolbox=None,
        )
        write_zip(package_root, zip_path)
        info = inspect_package(zip_path)
        assert_true(info["has_switch"], "package has switch")
        assert_true(not info["has_boot2"], "package has no boot2")
        assert_equal(info["config"]["grant_secret"], "repl...cret", "secret masked")

        install_root = tmp / "sdmc"
        installed = install_package(zip_path, install_root, confirm_boot2=False)
        assert_true((install_root / "switch/play-time-control/config.json").is_file(), "package installed")
        assert_equal(installed["package"]["config"]["control_mode"], "observe", "installed mode")

        boot_zip = tmp / "boot.zip"
        with zipfile.ZipFile(boot_zip, "w") as package:
            package.writestr("switch/play-time-control/config.json", '{"version":1,"control_mode":"grant","grant_secret":"secret"}')
            package.writestr("atmosphere/contents/4200000000BD2300/flags/boot2.flag", "")
            package.writestr("atmosphere/contents/4200000000BD2300/exefs.nsp", "x")
        try:
            install_package(boot_zip, tmp / "boot-dest", confirm_boot2=False)
        except ValueError as exc:
            assert_true("boot2" in str(exc), "boot2 confirmation error")
        else:
            raise AssertionError("boot2 install without confirmation should fail")


def main() -> int:
    test_requests_and_flag()
    test_token_generation_and_package_install()
    print("PTC frontend server tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
