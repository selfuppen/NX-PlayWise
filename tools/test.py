#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from playwise_version import read_playwise_version


ROOT = Path(__file__).resolve().parents[1]
APP_DIR = Path("switch") / "playwise"
CONTENT_DIR = Path("atmosphere") / "contents" / "4200000000BD2300"
PYTHON = sys.executable
PLAYWISE_VERSION = read_playwise_version(ROOT)


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
        "tests/devkit/test_playwise_version.py",
        "tests/devkit/test_release_version.py",
        "tests/devkit/test_package_remote.py",
        "tests/devkit/test_delivery_package.py",
        "tests/devkit/test_install_script.py",
        "tests/devkit/test_switch_ipc_lifecycle.py",
        "tests/devkit/test_switch_ui_glyphs.py",
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
        overlay = root / "playwise.ovl"
        exefs = root / "exefs.nsp"
        manifest = root / "release-manifest.json"
        nro.write_bytes(b"nro")
        overlay.write_bytes(b"ovl")
        exefs.write_bytes(b"nsp")
        manifest.write_text(json.dumps({"schema_version": 1, "playwise_version": PLAYWISE_VERSION, "commit": "a" * 40,
            "release_id": f"playwise-{PLAYWISE_VERSION}+aaaaaaaaaaaa", "profile": "release", "protocol_version": 1,
            "recovery_version": 1, "pctl_layout_version": 1, "build": {}, "verified_environment": {}}), encoding="utf-8")
        expected_manifest = read_json(manifest)
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
            "defaults/config.json",
            "defaults/auth.json",
            "defaults/rules.json",
            "defaults/state.json",
            "defaults/compatibility.json",
            "defaults/setup.json",
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
        config = read_json(package_app / "defaults" / "config.json")
        require("grant_secret" not in config and "control_mode" not in config, "release config must contain neither secrets nor legacy modes")
        require(not (package_app / "credentials.json").exists(), "package must generate credentials on the device")
        require(read_json(package_app / "defaults" / "setup.json")["phase"] == "unconfigured", "package must not take control before setup")
        require(read_json(package_app / "build.json") == expected_manifest, "package build manifest must match the generated manifest")
        require((out / CONTENT_DIR / "flags" / "boot2.flag").exists(), "package must contain boot2.flag")
        with zipfile.ZipFile(zip_path) as package:
            names = package.namelist()
        require("switch/playwise/defaults/config.json" in names, "playwise zip missing config defaults")
        require(not any(name in names for name in (
            "switch/playwise/config.json",
            "switch/playwise/auth.json",
            "switch/playwise/rules.json",
            "switch/playwise/state.json",
            "switch/playwise/compatibility.json",
            "switch/playwise/setup.json",
        )), "playwise zip must not overwrite live runtime data")
        require(not any(name.startswith("playwise-install/") for name in names), "playwise zip must not contain installer-only files")
        require("switch/.overlays/playwise.ovl" in names, "playwise package must contain the overlay")

        installed = root / "installed"
        (installed / APP_DIR).mkdir(parents=True)
        preserved = {
            "config.json": b'{"device_id":"existing"}',
            "auth.json": b'{"pin_hash":"existing"}',
            "rules.json": b'{"week":["existing"]}',
            "state.json": b'{"updated_at":123}',
            "compatibility.json": b'{"release_id":"old"}',
            "setup.json": b'{"phase":"active"}',
            "credentials.json": b'{"grant_secret":"private"}',
            "ledger/used_nonces.jsonl": b"existing-ledger\n",
            "logs/old.log": b"existing-log\n",
            "backups/install_pctl_snapshot.json": b"existing-backup",
        }
        for relative, data in preserved.items():
            path = installed / APP_DIR / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        old_nro = installed / APP_DIR / "pctc.nro"
        old_build = installed / APP_DIR / "build.json"
        old_nro.write_bytes(b"old-nro")
        old_build.write_bytes(b"old-build")
        shutil.copytree(out, installed, dirs_exist_ok=True)
        for relative, data in preserved.items():
            require((installed / APP_DIR / relative).read_bytes() == data,
                    f"direct overlay must preserve {relative}")
        require((installed / APP_DIR / "pctc.nro").read_bytes() == b"nro", "direct overlay must replace the Companion binary")
        require(read_json(installed / APP_DIR / "build.json") == expected_manifest,
                "direct overlay must replace build.json")
        require((installed / CONTENT_DIR / "exefs.nsp").read_bytes() == b"nsp",
                "direct overlay must replace the sysmodule binary")
        require((installed / "switch" / ".overlays" / "playwise.ovl").read_bytes() == b"ovl",
                "direct overlay must replace the Overlay binary")

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
