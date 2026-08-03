#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import struct
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


def valid_overlay() -> bytes:
    nro_size = 0x80
    nacp_offset = package_remote.NRO_ASSET_HEADER_SIZE
    data = bytearray(nro_size + nacp_offset + package_remote.NACP_SIZE)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_OFFSET, package_remote.NRO_MAGIC)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_SIZE_OFFSET, nro_size)
    struct.pack_into("<II", data, nro_size, package_remote.NRO_ASSET_MAGIC, 0)
    struct.pack_into("<QQ", data, nro_size + 0x18, nacp_offset, package_remote.NACP_SIZE)
    nacp_start = nro_size + nacp_offset
    data[nacp_start : nacp_start + 5] = b"PCTC\0"
    version_start = nacp_start + package_remote.NACP_DISPLAY_VERSION_OFFSET
    data[version_start : version_start + 6] = b"1.0.0\0"
    return bytes(data)


def overlay_without_assets() -> bytes:
    data = bytearray(0x80)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_OFFSET, package_remote.NRO_MAGIC)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_SIZE_OFFSET, len(data))
    return bytes(data)


def write_package(path: Path, mode: str, boot2: bool, overlay_data: bytes | None = None) -> None:
    with zipfile.ZipFile(path, "w") as package:
        package.writestr("switch/play-time-control/config.json", f'{{"control_mode":"{mode}"}}')
        package.writestr("switch/play-time-control/pctc.nro", b"nro")
        if boot2:
            package.writestr("switch/.overlays/pctc.ovl", valid_overlay() if overlay_data is None else overlay_data)
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

        invalid_path = root / "grant-boot2-20260730-120001.zip"
        write_package(invalid_path, "grant", True, overlay_without_assets())
        try:
            package_remote.verify_package_zip(invalid_path, "grant-boot2")
        except package_remote.PackageError as exc:
            require("missing NRO asset header" in str(exc), "missing NACP overlay must explain the failure")
        else:
            raise AssertionError("overlay without NACP metadata must be rejected")


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
