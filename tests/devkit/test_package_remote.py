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


def valid_nro(*, with_icon: bool, embedded_manifest: bytes = b"") -> bytes:
    nro_size = 0x80
    icon = b"JFIF" if with_icon else b""
    icon_offset = package_remote.NRO_ASSET_HEADER_SIZE
    nacp_offset = icon_offset + len(icon)
    data = bytearray(nro_size + nacp_offset + package_remote.NACP_SIZE)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_OFFSET, package_remote.NRO_MAGIC)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_SIZE_OFFSET, nro_size)
    struct.pack_into("<II", data, nro_size, package_remote.NRO_ASSET_MAGIC, 0)
    struct.pack_into("<QQ", data, nro_size + 0x08, icon_offset, len(icon))
    struct.pack_into("<QQ", data, nro_size + 0x18, nacp_offset, package_remote.NACP_SIZE)
    data[nro_size + icon_offset : nro_size + icon_offset + len(icon)] = icon
    nacp_start = nro_size + nacp_offset
    data[nacp_start : nacp_start + len(package_remote.APP_TITLE)] = package_remote.APP_TITLE
    version_start = nacp_start + package_remote.NACP_DISPLAY_VERSION_OFFSET
    data[version_start : version_start + 6] = b"0.1.0\0"
    return bytes(data) + embedded_manifest


def overlay_without_assets() -> bytes:
    data = bytearray(0x80)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_OFFSET, package_remote.NRO_MAGIC)
    struct.pack_into("<I", data, package_remote.NRO_HEADER_SIZE_OFFSET, len(data))
    return bytes(data)


def write_package(
    path: Path,
    boot2: bool,
    overlay_data: bytes | None = None,
    *,
    component_marker: bytes = b"",
) -> None:
    manifest = ('{"schema_version":1,"playwise_version":"0.1.0","commit":"' + "a" * 40 +
        '","release_id":"playwise-0.1.0+aaaaaaaaaaaa","profile":"release","protocol_version":1,'
        '"recovery_version":1,"pctl_layout_version":1,"build":{},"verified_environment":{}}')
    with zipfile.ZipFile(path, "w") as package:
        package.writestr("switch/playwise/config.json", '{"version":1,"device_id":"kid-switch"}')
        package.writestr("switch/playwise/build.json", manifest)
        package.writestr("playwise-install/release-manifest.json", manifest)
        embedded = manifest.encode()
        package.writestr("switch/playwise/pctc.nro", valid_nro(with_icon=True, embedded_manifest=embedded + component_marker))
        if boot2:
            package.writestr("switch/.overlays/pctc.ovl", valid_nro(with_icon=False, embedded_manifest=embedded) if overlay_data is None else overlay_data)
            package.writestr("atmosphere/contents/4200000000BD2300/exefs.nsp", embedded)
            package.writestr("atmosphere/contents/4200000000BD2300/flags/boot2.flag", b"")


def test_container_command() -> None:
    command = package_remote.container_command()
    require("/ws/playwise" in command, "container command must use the mounted repository")
    require("make test packages" in command, "container command must test and package")
    require("device-lab-package" in command, "authoritative build must verify the isolated Device Lab target")
    require("make clean" in command, "authoritative build must remove stale intermediates first")
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
        for prefix, boot2 in package_remote.PACKAGE_EXPECTATIONS.items():
            path = root / f"{prefix}-20260730-120000.zip"
            write_package(path, boot2)
            package_remote.verify_package_zip(path, prefix)

        invalid_path = root / "playwise-20260730-120001.zip"
        write_package(invalid_path, True, overlay_without_assets())
        try:
            package_remote.verify_package_zip(invalid_path, "playwise")
        except package_remote.PackageError as exc:
            require("missing NRO asset header" in str(exc), "missing NACP overlay must explain the failure")
        else:
            raise AssertionError("overlay without NACP metadata must be rejected")

        contaminated_path = root / "playwise-20260730-120002.zip"
        write_package(contaminated_path, True, component_marker=b"probe_suspend")
        try:
            package_remote.verify_package_zip(contaminated_path, "playwise")
        except package_remote.PackageError as exc:
            require("forbidden Release marker probe_suspend" in str(exc), "binary contamination must identify the marker")
        else:
            raise AssertionError("a Release binary containing a Device Lab handler marker must be rejected")


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
