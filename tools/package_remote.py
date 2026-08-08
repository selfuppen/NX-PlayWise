#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import struct
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SSH_HOST = "127.0.0.1"
DEFAULT_SSH_PORT = 1888
DEFAULT_SSH_USER = "root"
DEFAULT_CONTAINER_PATH = "/ws/playwise"
APP_CONFIG = "switch/playwise/config.json"
APP_BUILD = "switch/playwise/build.json"
RELEASE_MANIFEST = "playwise-install/release-manifest.json"
CONTENT_ROOT = "atmosphere/contents/4200000000BD2300"
DEVICE_LAB_CONTENT_ROOT = "atmosphere/contents/4200000000BD23F0"
DEVICE_LAB_PACKAGE = "playwise-device-lab-0.1.2-alpha.zip"
NRO_HEADER_OFFSET = 0x10
NRO_HEADER_SIZE_OFFSET = 0x18
NRO_HEADER_END = 0x80
NRO_ASSET_HEADER_SIZE = 0x38
NRO_MAGIC = 0x304F524E
NRO_ASSET_MAGIC = 0x54455341
NACP_SIZE = 0x4000
NACP_TITLE_SIZE = 0x200
NACP_DISPLAY_VERSION_OFFSET = 0x3060
APP_TITLE = "任你玩".encode("utf-8")
PLAYWISE_VERSION = "0.1.2-alpha"
PACKAGE_EXPECTATIONS = {"playwise": True}
RELEASE_COMPONENTS = (
    f"{CONTENT_ROOT}/exefs.nsp",
    "switch/playwise/pctc.nro",
    "switch/.overlays/pctc.ovl",
)
SCANNABLE_RELEASE_COMPONENTS = RELEASE_COMPONENTS[1:]
FORBIDDEN_RELEASE_MARKERS = (
    b"raw_block",
    b"probe_suspend",
    b"prepare_device_test",
    b"parent_unlock",
    b"set_bedtime",
    b"set_limit_action",
    b"block_today",
    b"capabilities.json",
    b"control_mode",
    b'"observe"',
    b'"grant"',
)
FORBIDDEN_SECRET_MARKERS = (b"replace-with-long-random-secret",)


class PackageError(RuntimeError):
    pass


def package_prefix(path: Path) -> str:
    for prefix in PACKAGE_EXPECTATIONS:
        if path.name.startswith(f"{prefix}-") and path.suffix == ".zip":
            return prefix
    raise PackageError(f"unexpected package name: {path.name}")


def safe_zip_members(package: zipfile.ZipFile) -> list[str]:
    names = package.namelist()
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts:
            raise PackageError(f"unsafe zip entry: {name}")
    return names


def verify_nro_asset(data: bytes, label: str, *, require_icon: bool) -> None:
    if len(data) < NRO_HEADER_END:
        raise PackageError(f"{label}: overlay is too small for an NRO header")
    magic = struct.unpack_from("<I", data, NRO_HEADER_OFFSET)[0]
    nro_size = struct.unpack_from("<I", data, NRO_HEADER_SIZE_OFFSET)[0]
    if magic != NRO_MAGIC:
        raise PackageError(f"{label}: missing NRO0 header")
    if nro_size < NRO_HEADER_END or nro_size + NRO_ASSET_HEADER_SIZE > len(data):
        raise PackageError(f"{label}: missing NRO asset header")

    asset_magic, asset_version = struct.unpack_from("<II", data, nro_size)
    if asset_magic != NRO_ASSET_MAGIC or asset_version != 0:
        raise PackageError(f"{label}: invalid NRO asset header")
    icon_offset, icon_size = struct.unpack_from("<QQ", data, nro_size + 0x08)
    nacp_offset, nacp_size = struct.unpack_from("<QQ", data, nro_size + 0x18)
    if require_icon and icon_size == 0:
        raise PackageError(f"{label}: missing icon asset")
    if icon_size and nro_size + icon_offset + icon_size > len(data):
        raise PackageError(f"{label}: invalid icon metadata bounds")
    if nacp_size != NACP_SIZE:
        raise PackageError(f"{label}: missing 0x4000-byte NACP metadata")
    nacp_start = nro_size + nacp_offset
    nacp_end = nacp_start + nacp_size
    if nacp_offset < NRO_ASSET_HEADER_SIZE or nacp_end > len(data):
        raise PackageError(f"{label}: invalid NACP metadata bounds")
    title = data[nacp_start : nacp_start + NACP_TITLE_SIZE].split(b"\0", 1)[0]
    if title != APP_TITLE:
        raise PackageError(f"{label}: NACP title must be 任你玩")
    display_version = data[nacp_start + NACP_DISPLAY_VERSION_OFFSET : nacp_start + NACP_DISPLAY_VERSION_OFFSET + 16].split(b"\0", 1)[0]
    if display_version.decode("ascii", errors="replace") != PLAYWISE_VERSION:
        raise PackageError(f"{label}: NACP version must be {PLAYWISE_VERSION}")


def verify_package_zip(path: Path, prefix: str | None = None) -> None:
    expected_prefix = prefix or package_prefix(path)
    expect_boot2 = PACKAGE_EXPECTATIONS[expected_prefix]
    with zipfile.ZipFile(path) as package:
        names = safe_zip_members(package)
        if APP_CONFIG not in names:
            raise PackageError(f"{path.name}: missing {APP_CONFIG}")
        config = json.loads(package.read(APP_CONFIG).decode("utf-8"))
        if APP_BUILD not in names or RELEASE_MANIFEST not in names:
            raise PackageError(f"{path.name}: missing build/release manifest assets")
        build = json.loads(package.read(APP_BUILD).decode("utf-8"))
        manifest = json.loads(package.read(RELEASE_MANIFEST).decode("utf-8"))
        component_data = {name: package.read(name) for name in RELEASE_COMPONENTS if name in names}
        nro_data = component_data.get("switch/playwise/pctc.nro")
        overlay_data = component_data.get("switch/.overlays/pctc.ovl")
        package_payloads = {name: package.read(name) for name in names if not name.endswith("/")}
    if any(key in config for key in ("grant_secret", "control_mode", "allow_unlimited_to_limited")):
        raise PackageError(f"{path.name}: config contains removed secret or mode fields")
    if "switch/playwise/credentials.json" in names or "switch/playwise/capabilities.json" in names:
        raise PackageError(f"{path.name}: release must not seed credentials or capabilities")
    if build != manifest or manifest.get("profile") != "release":
        raise PackageError(f"{path.name}: manifest copies differ or are not release profile")
    if manifest.get("playwise_version") != PLAYWISE_VERSION:
        raise PackageError(f"{path.name}: manifest version must be {PLAYWISE_VERSION}")
    for member_name, data in package_payloads.items():
        for marker in FORBIDDEN_SECRET_MARKERS:
            if marker in data:
                raise PackageError(f"{path.name}: {member_name} contains forbidden secret marker {marker.decode('ascii')}")
    boot2 = f"{CONTENT_ROOT}/flags/boot2.flag"
    exefs = f"{CONTENT_ROOT}/exefs.nsp"
    nro = "switch/playwise/pctc.nro"
    overlay = "switch/.overlays/pctc.ovl"
    if (boot2 in names) != expect_boot2:
        raise PackageError(f"{path.name}: unexpected boot2.flag state")
    if expect_boot2 and exefs not in names:
        raise PackageError(f"{path.name}: missing exefs.nsp")
    if nro not in names:
        raise PackageError(f"{path.name}: missing pctc.nro")
    if expect_boot2 and overlay not in names:
        raise PackageError(f"{path.name}: missing pctc.ovl")
    if expect_boot2 and overlay_data is not None:
        verify_nro_asset(overlay_data, f"{path.name}: pctc.ovl", require_icon=False)
    if nro_data is not None:
        verify_nro_asset(nro_data, f"{path.name}: pctc.nro", require_icon=True)
    embedded_manifest = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    for member_name in RELEASE_COMPONENTS:
        data = component_data.get(member_name)
        if data is None:
            raise PackageError(f"{path.name}: missing release component {member_name}")
    for member_name in SCANNABLE_RELEASE_COMPONENTS:
        data = component_data[member_name]
        if embedded_manifest not in data:
            raise PackageError(f"{path.name}: {member_name} does not embed the package manifest")
        for marker in FORBIDDEN_RELEASE_MARKERS:
            if marker in data:
                label = marker.decode("ascii")
                raise PackageError(f"{path.name}: {member_name} contains forbidden Release marker {label}")


def verify_device_lab_zip(path: Path) -> None:
    with zipfile.ZipFile(path) as package:
        names = safe_zip_members(package)
        build_path = "switch/playwise-device-lab/build.json"
        exefs_path = f"{DEVICE_LAB_CONTENT_ROOT}/exefs.nsp"
        nro_path = "switch/playwise-device-lab/playwise-device-lab.nro"
        required = {build_path, exefs_path, nro_path, "DEVICE-LAB.txt"}
        missing = sorted(required.difference(names))
        if missing:
            raise PackageError(f"{path.name}: missing Device Lab entries: {', '.join(missing)}")
        if any(name.endswith("/boot2.flag") for name in names):
            raise PackageError(f"{path.name}: Device Lab must not enable boot2.flag by default")
        manifest = json.loads(package.read(build_path).decode("utf-8"))
        if manifest.get("profile") != "device-lab" or manifest.get("playwise_version") != PLAYWISE_VERSION:
            raise PackageError(f"{path.name}: invalid Device Lab manifest profile or version")
        embedded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        if embedded not in package.read(nro_path):
            raise PackageError(f"{path.name}: {nro_path} does not embed the Device Lab manifest")


def verify_flat_sysmodule(path: Path, manifest: dict, *, release: bool) -> None:
    if not path.is_file():
        raise PackageError(f"missing uncompressed sysmodule verification image: {path}")
    data = path.read_bytes()
    embedded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    if embedded not in data:
        raise PackageError(f"{path.name}: sysmodule does not embed its generated manifest")
    if release:
        for marker in FORBIDDEN_RELEASE_MARKERS:
            if marker in data:
                raise PackageError(f"{path.name}: sysmodule contains forbidden Release marker {marker.decode('ascii')}")
        for marker in FORBIDDEN_SECRET_MARKERS:
            if marker in data:
                raise PackageError(f"{path.name}: sysmodule contains forbidden secret marker {marker.decode('ascii')}")


def verify_packaged_artifacts(path: Path) -> dict:
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read(RELEASE_MANIFEST).decode("utf-8"))
        expected = {
            f"{CONTENT_ROOT}/exefs.nsp": ROOT / "build" / "switch" / "exefs.nsp",
            "switch/playwise/pctc.nro": ROOT / "build" / "switch" / "pctc.nro",
            "switch/.overlays/pctc.ovl": ROOT / "build" / "switch" / "pctc.ovl",
        }
        for member_name, artifact in expected.items():
            if package.read(member_name) != artifact.read_bytes():
                raise PackageError(f"{path.name}: packaged {member_name} differs from the verified build artifact")
    return manifest


def latest_packages(package_dir: Path) -> dict[str, Path]:
    selected: dict[str, Path] = {}
    for prefix in PACKAGE_EXPECTATIONS:
        matches = sorted(package_dir.glob(f"{prefix}-*.zip"), key=lambda path: path.stat().st_mtime)
        if not matches:
            raise PackageError(f"missing generated package: {prefix}-*.zip")
        selected[prefix] = matches[-1]
    if len(list(package_dir.glob("*.zip"))) != 1:
        raise PackageError("release build must produce exactly one public zip")
    return selected


def container_command(container_path: str = DEFAULT_CONTAINER_PATH) -> str:
    path = "/opt/devkitpro/devkitA64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    container_script = (
        "export DEVKITPRO=/opt/devkitpro "
        "DEVKITARM=/opt/devkitpro/devkitARM "
        "DEVKITA64=/opt/devkitpro/devkitA64 "
        f"PATH={shlex.quote(path)} "
        f"&& cd {shlex.quote(container_path)} "
        "&& make clean "
        "&& make test packages device-lab-package"
    )
    return f"sh -lc {shlex.quote(container_script)}"


def ssh_command(
    host: str = DEFAULT_SSH_HOST,
    port: int = DEFAULT_SSH_PORT,
    user: str = DEFAULT_SSH_USER,
    container_path: str = DEFAULT_CONTAINER_PATH,
    identity: Path | None = None,
) -> list[str]:
    command = ["ssh", "-p", str(port), "-o", "ConnectTimeout=10"]
    if host in {"127.0.0.1", "localhost", "::1"}:
        # Local development containers can be recreated with a different host key.
        command.extend(["-o", "StrictHostKeyChecking=no", "-o", f"UserKnownHostsFile={os.devnull}"])
    if identity is not None:
        command.extend(["-i", str(identity)])
    command.extend([f"{user}@{host}", container_command(container_path)])
    return command


def run_container(
    host: str = DEFAULT_SSH_HOST,
    port: int = DEFAULT_SSH_PORT,
    user: str = DEFAULT_SSH_USER,
    container_path: str = DEFAULT_CONTAINER_PATH,
    identity: Path | None = None,
) -> None:
    process = subprocess.run(ssh_command(host, port, user, container_path, identity), cwd=ROOT, stdin=None)
    return_code = process.returncode
    if return_code != 0:
        raise PackageError(f"container package command failed with exit code {return_code}")


def remove_path(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def clean_package_results(package_dir: Path) -> None:
    package_dir = package_dir.resolve()
    expected_root = (ROOT / "build" / "packages").resolve()
    if package_dir != expected_root:
        raise PackageError(f"refusing to clean unexpected package directory: {package_dir}")
    remove_path(package_dir)
    package_dir.mkdir(parents=True)


def build_and_verify(
    host: str = DEFAULT_SSH_HOST,
    port: int = DEFAULT_SSH_PORT,
    user: str = DEFAULT_SSH_USER,
    container_path: str = DEFAULT_CONTAINER_PATH,
    identity: Path | None = None,
) -> None:
    package_dir = ROOT / "build" / "packages"
    device_lab_dir = ROOT / "build" / "device-lab"
    clean_package_results(package_dir)
    remove_path(device_lab_dir)
    run_container(host, port, user, container_path, identity)
    packages = latest_packages(package_dir)
    for prefix, path in packages.items():
        verify_package_zip(path, prefix)
        manifest = verify_packaged_artifacts(path)
        verify_flat_sysmodule(ROOT / "build" / "switch" / "pctc-sysmodule.bin", manifest, release=True)
    for child in package_dir.iterdir():
        if child.is_dir():
            remove_path(child)
    device_lab_package = device_lab_dir / DEVICE_LAB_PACKAGE
    verify_device_lab_zip(device_lab_package)
    with zipfile.ZipFile(device_lab_package) as package:
        lab_manifest = json.loads(package.read("switch/playwise-device-lab/build.json").decode("utf-8"))
    verify_flat_sysmodule(device_lab_dir / "switch" / "pwtl-sysmodule.bin", lab_manifest, release=False)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Clean, test, build, and verify the PlayWise Switch package in the local devkitPro container.")
    parser.add_argument("--host", default=DEFAULT_SSH_HOST, help=f"Container SSH host. Default: {DEFAULT_SSH_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_SSH_PORT, help=f"Container SSH port. Default: {DEFAULT_SSH_PORT}")
    parser.add_argument("--user", default=DEFAULT_SSH_USER, help=f"Container SSH user. Default: {DEFAULT_SSH_USER}")
    parser.add_argument(
        "--container-path",
        default=DEFAULT_CONTAINER_PATH,
        help=f"Mounted repository path in the container. Default: {DEFAULT_CONTAINER_PATH}",
    )
    parser.add_argument("--identity", type=Path, help="Optional SSH private key path.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        build_and_verify(args.host, args.port, args.user, args.container_path, args.identity)
    except (OSError, PackageError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        print(f"FAIL: container packages: {exc}")
        return 1
    print(f"PASS: container packages -> {ROOT / 'build' / 'packages'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
