#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import package_remote  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write_package(path: Path, mode: str, boot2: bool) -> None:
    with zipfile.ZipFile(path, "w") as package:
        package.writestr("switch/play-time-control/config.json", f'{{"control_mode":"{mode}"}}')
        package.writestr("switch/play-time-control/pctc.nro", b"nro")
        if boot2:
            package.writestr("atmosphere/contents/4200000000BD2300/exefs.nsp", b"nsp")
            package.writestr("atmosphere/contents/4200000000BD2300/flags/boot2.flag", b"")


def test_container_command() -> None:
    command = package_remote.container_command()
    require("/ws/switch-play-time-control-local" in command, "container command must use the mounted repository")
    require("make test packages" in command, "container command must test and package")
    require("--emit-bundle" not in command, "container command must not stream a copied bundle")
    require("git " not in command, "mounted local source must not require a git update")


def test_ssh_command() -> None:
    command = package_remote.ssh_command()
    require(command[0] == "ssh", "build transport must use OpenSSH")
    require("1888" in command, "default container SSH port must be 1888")
    require("root@127.0.0.1" in command, "default container SSH target must be local root")
    require(command[-1] == package_remote.container_command(), "SSH must execute the container build command")


def test_zip_verification() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-test-") as tmp_dir:
        root = Path(tmp_dir)
        for prefix, (mode, boot2) in package_remote.PACKAGE_EXPECTATIONS.items():
            path = root / f"{prefix}-20260730-120000.zip"
            write_package(path, mode, boot2)
            package_remote.verify_package_zip(path, prefix)


def test_clean_package_safety() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-failure-") as tmp_dir:
        try:
            package_remote.clean_package_results(Path(tmp_dir))
        except package_remote.PackageError:
            return
    raise AssertionError("cleaning outside build/packages must be rejected")


def main() -> int:
    test_container_command()
    test_ssh_command()
    test_zip_verification()
    test_clean_package_safety()
    print("Container package helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
