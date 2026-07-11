#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
APP_DIR = Path("switch") / "play-time-control"
CONTENT_DIR = Path("atmosphere") / "contents" / "4200000000BD2300"
PYTHON = sys.executable


class VerificationError(AssertionError):
    pass


class CommandError(RuntimeError):
    def __init__(self, command: list[str], result: subprocess.CompletedProcess[str]):
        self.command = command
        self.result = result
        super().__init__(f"command failed with exit code {result.returncode}: {format_command(command)}")


def format_command(command: list[str]) -> str:
    return " ".join(command)


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if check and result.returncode != 0:
        raise CommandError(command, result)
    return result


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise VerificationError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_true(value, label: str) -> None:
    if not value:
        raise VerificationError(f"{label}: expected truthy value")


def assert_missing(path: Path, label: str) -> None:
    if path.exists():
        raise VerificationError(f"{label}: expected missing path, found {path}")


def app_root(sdmc_root: Path) -> Path:
    return sdmc_root / APP_DIR


def latest_result(sdmc_root: Path, request_id: str) -> dict:
    result_path = app_root(sdmc_root) / "results" / f"{request_id}.json"
    assert_true(result_path.is_file(), f"result exists for {request_id}")
    return read_json(result_path)


def assert_observe_ok(result: dict, request_id: str, request_type: str) -> None:
    assert_equal(result["version"], 1, "result version")
    assert_equal(result["request_id"], request_id, "result request_id")
    assert_equal(result["type"], request_type, "result type")
    assert_equal(result["status"], "ok", "result status")
    assert_equal(result["mode"], "observe", "result mode")
    assert_equal(result["dry_run"], True, "result dry_run")


def verify_existing_python_tests() -> None:
    run([PYTHON, "tests/mvp/test_token_v1.py"])
    run([PYTHON, "tests/observe/test_observe_queue.py"])
    run([PYTHON, "tests/frontend/test_ptc_frontend_server.py"])


def verify_protocol_probe() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-auto-sdmc-") as tmp_dir:
        sdmc_root = Path(tmp_dir)
        day_index = "2380"
        run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "init",
                "--root",
                str(sdmc_root),
                "--device",
                "test-device",
                "--secret",
                "test-secret",
            ]
        )

        config = read_json(app_root(sdmc_root) / "config.json")
        assert_equal(config["control_mode"], "observe", "probe control_mode")
        capabilities = read_json(app_root(sdmc_root) / "capabilities.json")
        assert_equal(capabilities["play_timer_write_backend"], "pctl-s-v1", "probe play timer backend")

        code = run(
            [
                PYTHON,
                "tools/grant_code.py",
                "--minutes",
                "30",
                "--device",
                "test-device",
                "--secret",
                "test-secret",
                "--day-index",
                day_index,
                "--nonce",
                "4660",
            ]
        ).stdout.strip()
        assert_true(code, "grant code output")

        offline_id = run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "request",
                "--root",
                str(sdmc_root),
                "--type",
                "offline_code",
                "--code",
                code,
            ]
        ).stdout.strip()
        assert_true(offline_id, "offline request id")

        processed = run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "process-observe",
                "--root",
                str(sdmc_root),
                "--day-index",
                day_index,
            ]
        ).stdout.strip()
        assert_equal(processed, "processed=1", "offline process count")
        assert_true(app_root(sdmc_root).joinpath("inbox", "done", f"{offline_id}.json").is_file(), "offline request archived")
        offline_result = latest_result(sdmc_root, offline_id)
        assert_observe_ok(offline_result, offline_id, "offline_code")
        assert_equal(offline_result["applied"]["minutes"], 30, "offline applied minutes")
        assert_missing(app_root(sdmc_root).joinpath("ledger", "used_nonces.jsonl"), "observe nonce ledger")

        status_id = run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "request",
                "--root",
                str(sdmc_root),
                "--type",
                "status",
            ]
        ).stdout.strip()
        assert_true(status_id, "status request id")

        processed = run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "process-observe",
                "--root",
                str(sdmc_root),
                "--day-index",
                day_index,
            ]
        ).stdout.strip()
        assert_equal(processed, "processed=1", "status process count")
        assert_true(app_root(sdmc_root).joinpath("inbox", "done", f"{status_id}.json").is_file(), "status request archived")
        status_result = latest_result(sdmc_root, status_id)
        assert_observe_ok(status_result, status_id, "status")
        assert_missing(app_root(sdmc_root).joinpath("ledger", "used_nonces.jsonl"), "status observe nonce ledger")


def verify_safe_package() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-auto-package-") as tmp_dir:
        tmp = Path(tmp_dir)
        out = tmp / "safe"
        zip_path = tmp / "safe.zip"
        run(
            [
                PYTHON,
                "tools/package_sdmc.py",
                "--mode",
                "safe",
                "--device-id",
                "kid-switch",
                "--grant-secret",
                "replace-with-long-random-secret",
                "--max-add-minutes",
                "120",
                "--out",
                str(out),
                "--zip",
                str(zip_path),
            ]
        )

        package_app = out / APP_DIR
        for relative in [
            "config.json",
            "auth.json",
            "rules.json",
            "state.json",
            "capabilities.json",
            "inbox/pending",
            "inbox/processing",
            "inbox/done",
            "results",
            "logs",
            "ledger",
            "backups",
            "flags",
        ]:
            assert_true((package_app / relative).exists(), f"safe package contains {relative}")

        config = read_json(package_app / "config.json")
        assert_equal(config["control_mode"], "observe", "safe package control_mode")
        assert_equal(config["device_id"], "kid-switch", "safe package device_id")
        capabilities = read_json(package_app / "capabilities.json")
        assert_equal(capabilities["play_timer_write_backend"], "pctl-s-v1", "safe package play timer backend")
        assert_missing(out / CONTENT_DIR / "flags" / "boot2.flag", "safe boot2 flag")
        assert_true(zip_path.is_file(), "safe zip exists")

        with zipfile.ZipFile(zip_path) as package:
            names = package.namelist()
        assert_true("switch/play-time-control/config.json" in names, "safe zip config entry")
        assert_true(all(name.startswith("switch/") for name in names), "safe zip top-level entries")
        assert_true(not any(name.endswith("boot2.flag") for name in names), "safe zip has no boot2 flag")


def verify_boot2_requires_sysmodule() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-auto-boot2-") as tmp_dir:
        out = Path(tmp_dir) / "bad-boot2"
        result = run(
            [
                PYTHON,
                "tools/package_sdmc.py",
                "--mode",
                "disabled",
                "--out",
                str(out),
                "--boot2",
            ],
            check=False,
        )
        if result.returncode == 0:
            raise VerificationError("--boot2 without --sysmodule-exefs should fail")
        assert_true("--boot2 requires --sysmodule-exefs" in result.stderr, "boot2 failure message")
        assert_missing(out / CONTENT_DIR / "flags" / "boot2.flag", "failed boot2 flag")


def run_step(name: str, fn) -> bool:
    print(f"[RUN ] {name}")
    try:
        fn()
    except CommandError as exc:
        print(f"[FAIL] {name}")
        print(f"command: {format_command(exc.command)}")
        if exc.result.stdout:
            print("stdout:")
            print(exc.result.stdout.rstrip())
        if exc.result.stderr:
            print("stderr:")
            print(exc.result.stderr.rstrip())
        return False
    except Exception as exc:
        print(f"[FAIL] {name}")
        print(str(exc))
        return False
    print(f"[PASS] {name}")
    return True


def main() -> int:
    steps = [
        ("existing Python regressions", verify_existing_python_tests),
        ("host protocol probe", verify_protocol_probe),
        ("safe package layout", verify_safe_package),
        ("boot2 packaging guard", verify_boot2_requires_sysmodule),
    ]
    passed = 0
    for name, fn in steps:
        if run_step(name, fn):
            passed += 1
    total = len(steps)
    print(f"summary: {passed}/{total} automatic verification steps passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
