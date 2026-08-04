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
APP_CONFIG = "switch/play-time-control/config.json"
CONTENT_ROOT = "atmosphere/contents/4200000000BD2300"
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
PACKAGE_EXPECTATIONS = {
    "safe-nro": ("observe", False),
    "disabled-boot2": ("disabled", True),
    "observe-boot2": ("observe", True),
    "grant-boot2": ("grant", True),
    "enforce-boot2": ("enforce", True),
}


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
    if data[nacp_start + NACP_DISPLAY_VERSION_OFFSET] == 0:
        raise PackageError(f"{label}: NACP version must be non-empty")


def verify_package_zip(path: Path, prefix: str | None = None) -> None:
    expected_prefix = prefix or package_prefix(path)
    expected_mode, expect_boot2 = PACKAGE_EXPECTATIONS[expected_prefix]
    with zipfile.ZipFile(path) as package:
        names = safe_zip_members(package)
        if APP_CONFIG not in names:
            raise PackageError(f"{path.name}: missing {APP_CONFIG}")
        config = json.loads(package.read(APP_CONFIG).decode("utf-8"))
        nro_data = package.read("switch/play-time-control/pctc.nro") if "switch/play-time-control/pctc.nro" in names else None
        overlay_data = package.read("switch/.overlays/pctc.ovl") if "switch/.overlays/pctc.ovl" in names else None
    if config.get("control_mode") != expected_mode:
        raise PackageError(f"{path.name}: expected control_mode={expected_mode}")
    boot2 = f"{CONTENT_ROOT}/flags/boot2.flag"
    exefs = f"{CONTENT_ROOT}/exefs.nsp"
    nro = "switch/play-time-control/pctc.nro"
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


def latest_packages(package_dir: Path) -> dict[str, Path]:
    selected: dict[str, Path] = {}
    for prefix in PACKAGE_EXPECTATIONS:
        matches = sorted(package_dir.glob(f"{prefix}-*.zip"), key=lambda path: path.stat().st_mtime)
        if not matches:
            raise PackageError(f"missing generated package: {prefix}-*.zip")
        selected[prefix] = matches[-1]
    return selected


def container_command(container_path: str = DEFAULT_CONTAINER_PATH) -> str:
    path = "/opt/devkitpro/devkitA64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    container_script = (
        "export DEVKITPRO=/opt/devkitpro "
        "DEVKITARM=/opt/devkitpro/devkitARM "
        "DEVKITA64=/opt/devkitpro/devkitA64 "
        f"PATH={shlex.quote(path)} "
        f"&& cd {shlex.quote(container_path)} "
        "&& make test packages"
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
    clean_package_results(package_dir)
    run_container(host, port, user, container_path, identity)
    packages = latest_packages(package_dir)
    for prefix, path in packages.items():
        verify_package_zip(path, prefix)
    for child in package_dir.iterdir():
        if child.is_dir():
            remove_path(child)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Clean, test, build, and verify all Switch packages in the local devkitPro container.")
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
