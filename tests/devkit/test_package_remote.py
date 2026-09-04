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


def valid_nro(*, with_icon: bool, embedded_manifest: bytes = b"", title: bytes | None = None,
              display_version: str | None = None) -> bytes:
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
    app_title = package_remote.APP_TITLE if title is None else title
    data[nacp_start : nacp_start + len(app_title)] = app_title
    version_start = nacp_start + package_remote.NACP_DISPLAY_VERSION_OFFSET
    version_bytes = (package_remote.PLAYWISE_VERSION if display_version is None else display_version).encode("ascii") + b"\0"
    data[version_start : version_start + len(version_bytes)] = version_bytes
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
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    require("/ws/playwise" in command, "container command must use the mounted repository")
    require("test packages" in command, "container command must test and package")
    require("packages: package-complete device-lab-package" in makefile,
            "packages target must verify the isolated Device Lab target")
    require("make clean" not in command, "the default build must reuse valid intermediates")
    require("--emit-bundle" not in command, "container command must not stream a copied bundle")
    require("git " not in command, "mounted local source must not require a git update")
    require("eden-test-nro" in command, "the default build must include the emulator NRO target")
    require("CLEAN_EDEN=1" not in command, "the default build must keep valid Eden intermediates")

    clean = package_remote.container_command(clean=True)
    require("make clean" in clean, "explicit clean build must remove stale intermediates first")
    require("CLEAN_EDEN=1" in clean, "explicit clean build must remove Eden intermediates")

    without_eden = package_remote.container_command(with_eden=False)
    require("eden-test-nro" not in without_eden, "with_eden=False must omit Eden target")
    require("CLEAN_EDEN=1" not in without_eden, "with_eden=False must omit CLEAN_EDEN")

    incremental = package_remote.container_command(clean=False)
    require("make clean" not in incremental, "incremental build must omit make clean")
    require("test packages" in incremental, "incremental build must still compile packages")

    skip_tests = package_remote.container_command(run_tests=False)
    require("test" not in skip_tests.split("make -j ")[1].split(), "skip_tests must omit test target")

    only_lab = package_remote.container_command(only="device-lab")
    require("device-lab-package" in only_lab, "only=device-lab must target device-lab-package")
    require("eden-test-nro" not in only_lab, "only=device-lab must omit Eden")

    jobs_cmd = package_remote.container_command(jobs=4)
    require("make -j4 " in jobs_cmd, "explicit jobs must be reflected in make command")


def test_ssh_command() -> None:
    command = package_remote.ssh_command()
    require(command[0] == "ssh", "build transport must use OpenSSH")
    require("1888" in command, "default container SSH port must be 1888")
    require("root@127.0.0.1" in command, "default container SSH target must be local root")
    require(command[-1] == package_remote.container_command(), "SSH must execute the container build command")
    without_eden_command = package_remote.ssh_command(with_eden=False)
    require(without_eden_command[-1] == package_remote.container_command(with_eden=False),
            "without eden must reach container through same SSH transport")
    lab_command = package_remote.ssh_command(only="device-lab")
    require(lab_command[-1] == package_remote.container_command(only="device-lab"),
            "only=device-lab must propagate to container command")


def test_container_ssh_key_persistence() -> None:
    dockerfile = (ROOT / "docker" / "Dockerfile").read_text(encoding="utf-8")
    compose = (ROOT / "docker" / "docker-compose.yml").read_text(encoding="utf-8")
    require("install -d -m 0700 /root/.ssh" in dockerfile,
            "container image must initialize the SSH directory with a safe mode")
    require("chmod 0600 /root/.ssh/authorized_keys" in dockerfile,
            "container image must initialize authorized_keys with a safe mode")
    require("playwise-ssh-keys:/root/.ssh" in compose,
            "Compose must persist root SSH authorization outside the container layer")
    require("name: playwise-devkitpro-ssh-keys" in compose,
            "SSH authorization volume must remain stable across Compose recreation")


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

        eden_path = root / "playwise-20260730-120004.zip"
        eden_manifest = write_package(eden_path, True, component_marker=b"playwise-eden")
        try:
            package_remote.verify_package_zip(eden_path, "playwise", expected_manifest=eden_manifest)
        except package_remote.PackageError as exc:
            require("forbidden Eden marker playwise-eden" in str(exc),
                    "Eden marker rejection must explain the isolation boundary")
        else:
            raise AssertionError("a Release binary containing Eden markers must be rejected")

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


def test_eden_nro_verification() -> None:
    version = package_remote.PLAYWISE_VERSION
    manifest = {
        "schema_version": 1,
        "playwise_version": version,
        "commit": "a" * 40,
        "release_id": f"playwise-{version}+aaaaaaaaaaaa",
        "profile": "eden-test",
        "protocol_version": 1,
        "recovery_version": 1,
        "pctl_layout_version": 1,
        "build": {},
        "verified_environment": {},
    }
    embedded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    markers = b"EDEN TEST" + b"playwise-eden" + b"playwise-eden-test-secret-00000001"
    with tempfile.TemporaryDirectory(prefix="ptc-eden-nro-") as tmp_dir:
        root = Path(tmp_dir)
        good = root / package_remote.EDEN_NRO
        good.write_bytes(valid_nro(
            with_icon=True,
            embedded_manifest=embedded + markers,
            title=package_remote.EDEN_APP_TITLE,
        ))
        package_remote.verify_eden_nro(good, manifest)

        try:
            package_remote.verify_eden_nro(good, dict(manifest, profile="release"))
        except package_remote.PackageError as exc:
            require("invalid Eden manifest profile" in str(exc), "a Release manifest must not describe the Eden NRO")
        else:
            raise AssertionError("verifying the Eden NRO against a Release manifest must be rejected")

        unbadged = root / "unbadged.nro"
        unbadged.write_bytes(valid_nro(
            with_icon=True,
            embedded_manifest=embedded,
            title=package_remote.EDEN_APP_TITLE,
        ))
        try:
            package_remote.verify_eden_nro(unbadged, manifest)
        except package_remote.PackageError as exc:
            require("missing Eden marker" in str(exc), "an unbadged Eden NRO must name the missing marker")
        else:
            raise AssertionError("an Eden NRO without its on-screen badge must be rejected")

        mistitled = root / "mistitled.nro"
        mistitled.write_bytes(valid_nro(with_icon=True, embedded_manifest=embedded + markers))
        try:
            package_remote.verify_eden_nro(mistitled, manifest)
        except package_remote.PackageError as exc:
            require("NACP title must be" in str(exc), "an Eden NRO wearing the Release title must be rejected")
        else:
            raise AssertionError("an Eden NRO with the Release NACP title must be rejected")


def test_device_lab_zip_verification() -> None:
    manifest = {
        "schema_version": 1,
        "playwise_version": package_remote.PLAYWISE_VERSION,
        "commit": "b" * 40,
        "release_id": f"playwise-{package_remote.PLAYWISE_VERSION}+bbbbbbbbbbbb",
        "profile": "device-lab",
        "protocol_version": 1,
        "recovery_version": 1,
        "pctl_layout_version": 1,
        "build": {},
        "verified_environment": {},
    }
    embedded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    with tempfile.TemporaryDirectory(prefix="ptc-device-lab-package-") as tmp_dir:
        root = Path(tmp_dir)
        good = root / package_remote.DEVICE_LAB_PACKAGE
        with zipfile.ZipFile(good, "w") as package:
            package.writestr("switch/playwise-device-lab/build.json", json.dumps(manifest))
            package.writestr("switch/playwise-device-lab/playwise-device-lab.nro",
                valid_nro(with_icon=False, embedded_manifest=embedded + package_remote.DEVICE_LAB_NRO_UI_MARKER,
                    title=package_remote.DEVICE_LAB_NRO_TITLE,
                    display_version=f"{package_remote.PLAYWISE_VERSION}-lab"))
            package.writestr("switch/.overlays/playwise-device-lab.ovl",
                valid_nro(with_icon=False, embedded_manifest=embedded +
                    package_remote.DEVICE_LAB_OVERLAY_UI_MARKER + package_remote.DEVICE_LAB_ACTIVATION_AB_MARKER,
                    title=package_remote.DEVICE_LAB_OVERLAY_TITLE,
                    display_version=f"{package_remote.PLAYWISE_VERSION}-lab"))
            package.writestr(f"{package_remote.DEVICE_LAB_CONTENT_ROOT}/exefs.nsp", embedded)
            package.writestr(f"{package_remote.DEVICE_LAB_CONTENT_ROOT}/flags/", b"")
            package.writestr("DEVICE-LAB.txt", "任我玩 DEVICE LAB - 内部取证工具")
        package_remote.verify_device_lab_zip(good)

        missing_overlay = root / "missing-overlay.zip"
        with zipfile.ZipFile(good) as source, zipfile.ZipFile(missing_overlay, "w") as destination:
            for name in source.namelist():
                if name != "switch/.overlays/playwise-device-lab.ovl":
                    destination.writestr(name, source.read(name))
        try:
            package_remote.verify_device_lab_zip(missing_overlay)
        except package_remote.PackageError as exc:
            require("playwise-device-lab.ovl" in str(exc), "missing Lab Overlay must be identified")
        else:
            raise AssertionError("Device Lab package without its dedicated Overlay must be rejected")

        with zipfile.ZipFile(good, "a") as package:
            package.writestr(f"{package_remote.DEVICE_LAB_CONTENT_ROOT}/flags/boot2.flag", b"")
        try:
            package_remote.verify_device_lab_zip(good)
        except package_remote.PackageError as exc:
            require("must not enable boot2.flag" in str(exc), "Lab package boot flag rejection must be explicit")
        else:
            raise AssertionError("Device Lab package must remain opt-in")


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
        device_lab = root / package_remote.DEVICE_LAB_PACKAGE
        standard.write_bytes(b"standard")
        complete.write_bytes(b"complete")
        device_lab.write_bytes(b"device-lab")
        selected = package_remote.latest_packages(root)
        require(selected == {"playwise": standard, "complete": complete, "device_lab": device_lab},
                "build outputs must be selected by exact versioned names")

        single_dir = root / "single"
        single_dir.mkdir()
        single_lab = single_dir / package_remote.DEVICE_LAB_PACKAGE
        single_lab.write_bytes(b"lab")
        lab_selected = package_remote.latest_packages(single_dir, target_packages={"device_lab"})
        require(lab_selected == {"device_lab": single_lab}, "single target selection must succeed")

        extra = root / "unexpected.zip"
        extra.write_bytes(b"extra")
        try:
            package_remote.latest_packages(root)
        except package_remote.PackageError as exc:
            require("exactly the standard, complete and device lab public zips" in str(exc),
                    "extra Zip must explain the output contract")
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
    test_container_ssh_key_persistence()
    test_zip_verification()
    test_eden_nro_verification()
    test_device_lab_zip_verification()
    test_clean_package_safety()
    test_public_package_selection()
    print("Container package helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
