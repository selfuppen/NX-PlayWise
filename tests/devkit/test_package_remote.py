#!/usr/bin/env python3
from __future__ import annotations

import json
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
    display_version = package_remote.PLAYWISE_VERSION.encode("ascii") + b"\0"
    data[version_start : version_start + len(display_version)] = display_version
    return bytes(data) + embedded_manifest + b"".join(package_remote.NRO_INFORMATION_MARKERS)


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
) -> dict:
    version = package_remote.PLAYWISE_VERSION
    manifest = (f'{{"schema_version":1,"playwise_version":"{version}","commit":"' + "a" * 40 +
        f'","release_id":"playwise-{version}+aaaaaaaaaaaa","profile":"release","protocol_version":1,'
        '"recovery_version":1,"pctl_layout_version":1,"build":{},"verified_environment":{}}')
    with zipfile.ZipFile(path, "w") as package:
        package.writestr("switch/playwise/defaults/config.json", '{"version":1,"device_id":"kid-switch"}')
        package.writestr("switch/playwise/defaults/auth.json", '{"version":1,"pin_hash":"","pin_salt":"","hash":"hmac-sha256","updated_at":0,"failed_attempts":0,"cooldown_until":0}')
        package.writestr("switch/playwise/defaults/rules.json", '{"version":1,"week":[]}')
        package.writestr("switch/playwise/defaults/state.json", '{"version":1}')
        package.writestr("switch/playwise/defaults/compatibility.json", '{"version":1}')
        package.writestr("switch/playwise/defaults/setup.json", '{"version":1,"phase":"unconfigured"}')
        package.writestr("switch/playwise/build.json", manifest)
        embedded = manifest.encode()
        package.writestr("switch/playwise/pctc.nro", valid_nro(with_icon=True, embedded_manifest=embedded + component_marker))
        if boot2:
            package.writestr("switch/.overlays/playwise.ovl", valid_nro(with_icon=False, embedded_manifest=embedded) if overlay_data is None else overlay_data)
            package.writestr("atmosphere/contents/4200000000BD2300/exefs.nsp", embedded)
            package.writestr("atmosphere/contents/4200000000BD2300/flags/boot2.flag", b"")
    return json.loads(manifest)


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
            manifest = write_package(path, boot2)
            package_remote.verify_package_zip(path, prefix, expected_manifest=manifest)

        invalid_path = root / "playwise-20260730-120001.zip"
        invalid_manifest = write_package(invalid_path, True, overlay_without_assets())
        try:
            package_remote.verify_package_zip(invalid_path, "playwise", expected_manifest=invalid_manifest)
        except package_remote.PackageError as exc:
            require("missing NRO asset header" in str(exc), "missing NACP overlay must explain the failure")
        else:
            raise AssertionError("overlay without NACP metadata must be rejected")

        contaminated_path = root / "playwise-20260730-120002.zip"
        contaminated_manifest = write_package(contaminated_path, True, component_marker=b"probe_suspend")
        try:
            package_remote.verify_package_zip(contaminated_path, "playwise", expected_manifest=contaminated_manifest)
        except package_remote.PackageError as exc:
            require("forbidden Release marker probe_suspend" in str(exc), "binary contamination must identify the marker")
        else:
            raise AssertionError("a Release binary containing a Device Lab handler marker must be rejected")

        mutable_path = root / "playwise-20260730-120003.zip"
        mutable_manifest = write_package(mutable_path, True)
        with zipfile.ZipFile(mutable_path, "a") as package:
            package.writestr("switch/playwise/auth.json", "{}")
        try:
            package_remote.verify_package_zip(mutable_path, "playwise", expected_manifest=mutable_manifest)
        except package_remote.PackageError as exc:
            require("must not overwrite runtime data" in str(exc), "runtime seed rejection must explain data preservation")
        else:
            raise AssertionError("a package containing a live mutable seed must be rejected")


def test_clean_package_safety() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-package-failure-") as tmp_dir:
        try:
            package_remote.clean_package_results(Path(tmp_dir))
        except package_remote.PackageError:
            return
    raise AssertionError("cleaning outside build/packages must be rejected")


def test_public_package_selection() -> None:
    with tempfile.TemporaryDirectory(prefix="ptc-public-packages-") as tmp_dir:
        root = Path(tmp_dir)
        standard = root / package_remote.STANDARD_PACKAGE
        complete = root / package_remote.COMPLETE_PACKAGE
        standard.write_bytes(b"standard")
        complete.write_bytes(b"complete")
        selected = package_remote.latest_packages(root)
        require(selected == {"playwise": standard, "complete": complete}, "public outputs must be selected by exact versioned names")
        extra = root / "unexpected.zip"
        extra.write_bytes(b"extra")
        try:
            package_remote.latest_packages(root)
        except package_remote.PackageError as exc:
            require("exactly the standard and complete public zips" in str(exc), "extra public Zip must explain the output contract")
        else:
            raise AssertionError("an extra public Zip must be rejected")
        extra.unlink()
        complete.unlink()
        try:
            package_remote.latest_packages(root)
        except package_remote.PackageError as exc:
            require(package_remote.COMPLETE_PACKAGE in str(exc), "missing complete package must identify its exact name")
        else:
            raise AssertionError("a missing complete package must be rejected")


def main() -> int:
    test_container_command()
    test_ssh_command()
    test_zip_verification()
    test_clean_package_safety()
    test_public_package_selection()
    print("Container package helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
