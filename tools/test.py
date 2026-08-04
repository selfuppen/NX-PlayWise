#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
APP_DIR = Path("switch") / "playwise"
CONTENT_DIR = Path("atmosphere") / "contents" / "4200000000BD2300"
PYTHON = sys.executable


class CheckError(AssertionError):
    pass


class CommandError(RuntimeError):
    def __init__(self, command: list[str], result: subprocess.CompletedProcess[str]):
        self.command = command
        self.result = result
        super().__init__(f"command failed with exit code {result.returncode}: {' '.join(command)}")


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if check and result.returncode != 0:
        raise CommandError(command, result)
    return result


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CheckError(message)


def app_root(sdmc_root: Path) -> Path:
    return sdmc_root / APP_DIR


def run_python_regressions() -> None:
    for test in [
        "tests/mvp/test_token_v1.py",
        "tests/mvp/test_token_v2.py",
        "tests/observe/test_observe_queue.py",
        "tests/frontend/test_ptc_frontend_server.py",
        "tests/devkit/test_package_remote.py",
    ]:
        run([PYTHON, test])


def verify_protocol_smoke() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-test-sdmc-") as tmp_dir:
        sdmc_root = Path(tmp_dir)
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
        require(config["control_mode"] == "observe", "protocol init must default to observe")
        capabilities = read_json(app_root(sdmc_root) / "capabilities.json")
        require(capabilities["play_timer_write_backend"] == "pctl-s-v2", "protocol init must use the current PCTL backend")

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
                "2380",
                "--nonce",
                "4660",
            ]
        ).stdout.strip()
        request_id = run(
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
        processed = run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "process-observe",
                "--root",
                str(sdmc_root),
                "--day-index",
                "2380",
            ]
        ).stdout.strip()
        require(processed == "processed=1", "observe smoke must process one request")
        require(
            app_root(sdmc_root).joinpath("inbox", "done", f"{request_id}.json").is_file(),
            "observe smoke must archive the request",
        )
        result = read_json(app_root(sdmc_root) / "results" / f"{request_id}.json")
        require(result["status"] == "ok", "observe smoke result must succeed")
        require(result["mode"] == "observe", "observe smoke result must report observe")
        require(result["dry_run"] is True, "observe smoke result must be dry-run")
        require(result["applied"]["minutes"] == 30, "observe smoke must preserve token minutes")
        require(
            not app_root(sdmc_root).joinpath("ledger", "used_nonces.jsonl").exists(),
            "observe smoke must not consume nonce",
        )

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
        processed = run(
            [
                PYTHON,
                "tools/protocol_probe.py",
                "process-observe",
                "--root",
                str(sdmc_root),
                "--day-index",
                "2380",
            ]
        ).stdout.strip()
        require(processed == "processed=1", "status smoke must process one request")
        status_result = read_json(app_root(sdmc_root) / "results" / f"{status_id}.json")
        require(status_result["status"] == "ok", "status smoke result must succeed")
        require(status_result["mode"] == "observe", "status smoke result must report observe")
        require(status_result["dry_run"] is True, "status smoke result must be dry-run")


def verify_safe_package() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-test-package-") as tmp_dir:
        root = Path(tmp_dir)
        out = root / "safe"
        zip_path = root / "safe.zip"
        run(
            [
                PYTHON,
                "tools/package_sdmc.py",
                "--mode",
                "safe",
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
            require((package_app / relative).exists(), f"safe package missing {relative}")
        require(read_json(package_app / "config.json")["control_mode"] == "observe", "safe package must observe")
        require(not (out / CONTENT_DIR / "flags" / "boot2.flag").exists(), "safe package must not contain boot2.flag")
        with zipfile.ZipFile(zip_path) as package:
            names = package.namelist()
        require("switch/playwise/config.json" in names, "safe zip missing config.json")
        require(all(name.startswith("switch/") for name in names), "safe zip may only contain switch entries")
        require("switch/.overlays/pctc.ovl" not in names, "safe package must not depend on Tesla")

        invalid_out = root / "invalid-boot2"
        invalid = run(
            [
                PYTHON,
                "tools/package_sdmc.py",
                "--mode",
                "disabled",
                "--out",
                str(invalid_out),
                "--boot2",
            ],
            check=False,
        )
        require(invalid.returncode != 0, "boot2 without sysmodule must fail")
        require("--boot2 requires --sysmodule-exefs" in invalid.stderr, "boot2 failure must explain the missing sysmodule")


def main() -> int:
    checks = [
        ("Python regressions", run_python_regressions),
        ("protocol smoke", verify_protocol_smoke),
        ("safe package", verify_safe_package),
    ]
    for name, check in checks:
        try:
            check()
        except CommandError as exc:
            print(f"FAIL: {name}")
            print(f"command: {' '.join(exc.command)}")
            if exc.result.stdout:
                print(exc.result.stdout.rstrip())
            if exc.result.stderr:
                print(exc.result.stderr.rstrip())
            return 1
        except Exception as exc:
            print(f"FAIL: {name}: {exc}")
            return 1
    print("PASS: local tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
