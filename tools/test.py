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
        "tests/frontend/test_ptc_frontend_server.py",
        "tests/devkit/test_package_remote.py",
        "tests/devkit/test_install_script.py",
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
        require("grant_secret" not in config and "control_mode" not in config, "release config must be non-secret and mode-free")
        credentials = read_json(app_root(sdmc_root) / "credentials.json")
        require(credentials["grant_secret"] == "test-secret", "protocol probe must isolate credentials")
        compatibility = read_json(app_root(sdmc_root) / "compatibility.json")
        require(compatibility["status"] == "pending", "new environment must await compatibility confirmation")
        setup = read_json(app_root(sdmc_root) / "setup.json")
        require(setup["phase"] == "unconfigured", "protocol init must not take control")
        request_id = run(
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
        pending = read_json(app_root(sdmc_root) / "inbox" / "pending" / f"{request_id}.json")
        require(pending["type"] == "status", "probe must write a release status request atomically")


def verify_playwise_package() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-test-package-") as tmp_dir:
        root = Path(tmp_dir)
        out = root / "playwise"
        zip_path = root / "playwise.zip"
        nro = root / "pctc.nro"
        overlay = root / "pctc.ovl"
        exefs = root / "exefs.nsp"
        manifest = root / "release-manifest.json"
        nro.write_bytes(b"nro")
        overlay.write_bytes(b"ovl")
        exefs.write_bytes(b"nsp")
        manifest.write_text(json.dumps({"schema_version": 1, "playwise_version": "0.1.1", "commit": "a" * 40,
            "release_id": "playwise-0.1.1+aaaaaaaaaaaa", "profile": "release", "protocol_version": 1,
            "recovery_version": 1, "pctl_layout_version": 1, "build": {}, "verified_environment": {}}), encoding="utf-8")
        run(
            [
                PYTHON,
                "tools/package_sdmc.py",
                "--out",
                str(out),
                "--zip",
                str(zip_path),
                "--nro",
                str(nro),
                "--overlay",
                str(overlay),
                "--sysmodule-exefs",
                str(exefs),
                "--manifest",
                str(manifest),
                "--boot2",
            ]
        )
        package_app = out / APP_DIR
        for relative in [
            "config.json",
            "auth.json",
            "rules.json",
            "state.json",
            "compatibility.json",
            "setup.json",
            "inbox/pending",
            "inbox/processing",
            "inbox/done",
            "results",
            "logs",
            "ledger",
            "backups",
            "flags",
            "support",
        ]:
            require((package_app / relative).exists(), f"playwise package missing {relative}")
        config = read_json(package_app / "config.json")
        require("grant_secret" not in config and "control_mode" not in config, "release config must contain neither secrets nor legacy modes")
        require(not (package_app / "credentials.json").exists(), "package must generate credentials on the device")
        require(read_json(package_app / "setup.json")["phase"] == "unconfigured", "package must not take control before setup")
        require((out / CONTENT_DIR / "flags" / "boot2.flag").exists(), "package must contain boot2.flag")
        with zipfile.ZipFile(zip_path) as package:
            names = package.namelist()
        require("switch/playwise/config.json" in names, "playwise zip missing config.json")
        require("playwise-install/release-manifest.json" in names, "playwise zip missing release manifest")
        require("switch/.overlays/pctc.ovl" in names, "playwise package must contain the overlay")

        invalid_out = root / "invalid-boot2"
        invalid = run(
            [
                PYTHON,
                "tools/package_sdmc.py",
                "--out",
                str(invalid_out),
                "--manifest",
                str(manifest),
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
        ("playwise package", verify_playwise_package),
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
