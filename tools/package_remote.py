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

from playwise_version import read_playwise_version


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SSH_HOST = "127.0.0.1"
DEFAULT_SSH_PORT = 1888
DEFAULT_SSH_USER = "root"
DEFAULT_CONTAINER_PATH = "/ws/playwise"
APP_DEFAULTS = "switch/playwise/defaults"
APP_BUILD = "switch/playwise/build.json"
APP_DEFAULT_FILES = tuple(f"{APP_DEFAULTS}/{name}" for name in (
    "config.json",
    "auth.json",
    "rules.json",
    "state.json",
    "compatibility.json",
    "setup.json",
))
APP_MUTABLE_SEEDS = tuple(f"switch/playwise/{name}" for name in (
    "config.json",
    "auth.json",
    "rules.json",
    "state.json",
    "compatibility.json",
    "setup.json",
))
CONTENT_ROOT = "atmosphere/contents/4200000000BD2300"
DEVICE_LAB_CONTENT_ROOT = "atmosphere/contents/4200000000BD23F0"
PACKAGE_ROOTS = ("switch/", "atmosphere/")
NRO_HEADER_OFFSET = 0x10
NRO_HEADER_SIZE_OFFSET = 0x18
NRO_HEADER_END = 0x80
NRO_ASSET_HEADER_SIZE = 0x38
NRO_MAGIC = 0x304F524E
NRO_ASSET_MAGIC = 0x54455341
NACP_SIZE = 0x4000
NACP_TITLE_SIZE = 0x200
NACP_DISPLAY_VERSION_OFFSET = 0x3060
APP_TITLE = "任我玩".encode("utf-8")
EDEN_APP_TITLE = b"PlayWise Eden Test"
DEVICE_LAB_NRO_TITLE = b"PlayWise DEVICE LAB"
DEVICE_LAB_OVERLAY_TITLE = b"PlayWise Device Lab"
DEVICE_LAB_NRO_UI_MARKER = "准备启用实验后台".encode("utf-8")
DEVICE_LAB_OVERLAY_UI_MARKER = "资格批次（推荐，连续四项）".encode("utf-8")
DEVICE_LAB_ACTIVATION_AB_MARKER = "自由：Timer 激活 A/B".encode("utf-8")
DEVICE_LAB_WARNING_MARKER = "内部取证工具".encode("utf-8")
PLAYWISE_VERSION = read_playwise_version(ROOT)
STANDARD_PACKAGE = f"playwise-{PLAYWISE_VERSION}.zip"
COMPLETE_PACKAGE = f"playwise-complete-{PLAYWISE_VERSION}.zip"
OFFLINE_HTML = ROOT / "tools" / "ptc_frontend" / "playwise-offline.html"
DEVICE_LAB_PACKAGE = f"playwise-device-lab-{PLAYWISE_VERSION}.zip"
EDEN_NRO = "pctc-eden.nro"
PACKAGE_EXPECTATIONS = {"playwise": True}
RELEASE_COMPONENTS = (
    f"{CONTENT_ROOT}/exefs.nsp",
    "switch/playwise/pctc.nro",
    "switch/.overlays/playwise.ovl",
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
    b"lab_session_start",
    b"lab_phase_start",
    b"lab_session_restore",
    b"restriction_effect",
    b"timer_activation_ab",
    b"GetPlayTimerSpentTimeForTest",
)
FORBIDDEN_SECRET_MARKERS = (b"replace-with-long-random-secret",)
# The emulator build shares its sources with Release, so scan every public
# artifact for its profile, app root and fixed test secret.
FORBIDDEN_EDEN_MARKERS = (
    b"PLAYWISE_EDEN",
    b"eden-test",
    b"playwise-eden",
    b"playwise-eden-test-secret-00000001",
)
NRO_INFORMATION_MARKERS = (
    "软件信息".encode("utf-8"),
    b"https://github.com/selfuppen/NX-PlayWise",
    b"https://selfuppen.github.io/NX-PlayWise/",
)


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


def verify_nro_asset(
    data: bytes,
    label: str,
    *,
    require_icon: bool,
    expected_title: bytes = APP_TITLE,
    expected_version: str = PLAYWISE_VERSION,
) -> None:
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
    if title != expected_title:
        raise PackageError(f"{label}: NACP title must be {expected_title.decode('utf-8')}")
    display_version = data[nacp_start + NACP_DISPLAY_VERSION_OFFSET : nacp_start + NACP_DISPLAY_VERSION_OFFSET + 16].split(b"\0", 1)[0]
    if display_version.decode("ascii", errors="replace") != expected_version:
        raise PackageError(f"{label}: NACP version must be {expected_version}")


def verify_eden_nro(path: Path, manifest: dict) -> None:
    """Verify the emulator-only NRO is present, self-identifying and isolated."""
    if manifest.get("profile") != "eden-test" or manifest.get("playwise_version") != PLAYWISE_VERSION:
        raise PackageError("invalid Eden manifest profile or version")
    if not path.is_file():
        raise PackageError(f"missing Eden NRO: {path}")
    data = path.read_bytes()
    verify_nro_asset(data, path.name, require_icon=True, expected_title=EDEN_APP_TITLE)
    embedded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    if embedded not in data:
        raise PackageError(f"{path.name}: does not embed the Eden manifest")
    for marker in (b"EDEN TEST", b"playwise-eden", b"playwise-eden-test-secret-00000001"):
        if marker not in data:
            raise PackageError(f"{path.name}: missing Eden marker {marker.decode('ascii')}")


def verify_package_zip(
    path: Path,
    prefix: str | None = None,
    *,
    expected_manifest: dict | None = None,
) -> None:
    expected_prefix = prefix or package_prefix(path)
    expect_boot2 = PACKAGE_EXPECTATIONS[expected_prefix]
    with zipfile.ZipFile(path) as package:
        names = safe_zip_members(package)
        unexpected = [name for name in names if not name.startswith(PACKAGE_ROOTS)]
        if unexpected:
            raise PackageError(f"{path.name}: package contains non-runtime entries: {', '.join(unexpected)}")
        missing_defaults = [name for name in APP_DEFAULT_FILES if name not in names]
        if missing_defaults:
            raise PackageError(f"{path.name}: missing install defaults: {', '.join(missing_defaults)}")
        forbidden_mutable = [name for name in APP_MUTABLE_SEEDS if name in names]
        if forbidden_mutable:
            raise PackageError(f"{path.name}: package must not overwrite runtime data: {', '.join(forbidden_mutable)}")
        config = json.loads(package.read(f"{APP_DEFAULTS}/config.json").decode("utf-8"))
        for default_name in APP_DEFAULT_FILES[1:]:
            json.loads(package.read(default_name).decode("utf-8"))
        if APP_BUILD not in names:
            raise PackageError(f"{path.name}: missing {APP_BUILD}")
        build = json.loads(package.read(APP_BUILD).decode("utf-8"))
        manifest = build if expected_manifest is None else expected_manifest
        component_data = {name: package.read(name) for name in RELEASE_COMPONENTS if name in names}
        nro_data = component_data.get("switch/playwise/pctc.nro")
        overlay_data = component_data.get("switch/.overlays/playwise.ovl")
        package_payloads = {name: package.read(name) for name in names if not name.endswith("/")}
    if any(key in config for key in ("grant_secret", "control_mode", "allow_unlimited_to_limited")):
        raise PackageError(f"{path.name}: config contains removed secret or mode fields")
    if "switch/playwise/credentials.json" in names or "switch/playwise/capabilities.json" in names:
        raise PackageError(f"{path.name}: release must not seed credentials or capabilities")
    if expected_manifest is not None and build != expected_manifest:
        raise PackageError(f"{path.name}: package build.json differs from the generated release manifest")
    if manifest.get("profile") != "release":
        raise PackageError(f"{path.name}: build manifest is not a release profile")
    if manifest.get("playwise_version") != PLAYWISE_VERSION:
        raise PackageError(f"{path.name}: manifest version must be {PLAYWISE_VERSION}")
    for member_name, data in package_payloads.items():
        for marker in FORBIDDEN_SECRET_MARKERS:
            if marker in data:
                raise PackageError(f"{path.name}: {member_name} contains forbidden secret marker {marker.decode('ascii')}")
        for marker in FORBIDDEN_EDEN_MARKERS:
            if marker in data:
                raise PackageError(f"{path.name}: {member_name} contains forbidden Eden marker {marker.decode('ascii')}")
    boot2 = f"{CONTENT_ROOT}/flags/boot2.flag"
    exefs = f"{CONTENT_ROOT}/exefs.nsp"
    nro = "switch/playwise/pctc.nro"
    overlay = "switch/.overlays/playwise.ovl"
    if (boot2 in names) != expect_boot2:
        raise PackageError(f"{path.name}: unexpected boot2.flag state")
    if expect_boot2 and exefs not in names:
        raise PackageError(f"{path.name}: missing exefs.nsp")
    if nro not in names:
        raise PackageError(f"{path.name}: missing pctc.nro")
    if expect_boot2 and overlay not in names:
        raise PackageError(f"{path.name}: missing playwise.ovl")
    if expect_boot2 and overlay_data is not None:
        verify_nro_asset(overlay_data, f"{path.name}: playwise.ovl", require_icon=False)
    if nro_data is not None:
        verify_nro_asset(nro_data, f"{path.name}: pctc.nro", require_icon=True)
        for marker in NRO_INFORMATION_MARKERS:
            if marker not in nro_data:
                raise PackageError(f"{path.name}: pctc.nro is missing software information marker {marker!r}")
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
        overlay_path = "switch/.overlays/playwise-device-lab.ovl"
        flags_dir = f"{DEVICE_LAB_CONTENT_ROOT}/flags/"
        required = {build_path, exefs_path, nro_path, overlay_path, flags_dir, "DEVICE-LAB.txt"}
        missing = sorted(required.difference(names))
        if missing:
            raise PackageError(f"{path.name}: missing Device Lab entries: {', '.join(missing)}")
        if any(name.endswith("/boot2.flag") for name in names):
            raise PackageError(f"{path.name}: Device Lab must not enable boot2.flag by default")
        manifest = json.loads(package.read(build_path).decode("utf-8"))
        if manifest.get("profile") != "device-lab" or manifest.get("playwise_version") != PLAYWISE_VERSION:
            raise PackageError(f"{path.name}: invalid Device Lab manifest profile or version")
        embedded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        nro_data = package.read(nro_path)
        overlay_data = package.read(overlay_path)
        if embedded not in nro_data:
            raise PackageError(f"{path.name}: {nro_path} does not embed the Device Lab manifest")
        if embedded not in overlay_data:
            raise PackageError(f"{path.name}: {overlay_path} does not embed the Device Lab manifest")
        if DEVICE_LAB_NRO_UI_MARKER not in nro_data:
            raise PackageError(f"{path.name}: {nro_path} does not contain the Chinese guided UI")
        if DEVICE_LAB_OVERLAY_UI_MARKER not in overlay_data:
            raise PackageError(f"{path.name}: {overlay_path} does not contain the Chinese guided UI")
        if DEVICE_LAB_ACTIVATION_AB_MARKER not in overlay_data:
            raise PackageError(f"{path.name}: {overlay_path} does not contain the timer activation A/B UI")
        if DEVICE_LAB_WARNING_MARKER not in package.read("DEVICE-LAB.txt"):
            raise PackageError(f"{path.name}: DEVICE-LAB.txt is not localized")
        lab_version = f"{PLAYWISE_VERSION}-lab"
        verify_nro_asset(package.read(nro_path), f"{path.name}: playwise-device-lab.nro", require_icon=False,
            expected_title=DEVICE_LAB_NRO_TITLE, expected_version=lab_version)
        verify_nro_asset(package.read(overlay_path), f"{path.name}: playwise-device-lab.ovl", require_icon=False,
            expected_title=DEVICE_LAB_OVERLAY_TITLE, expected_version=lab_version)


def verify_complete_package(path: Path, standard_package: Path, offline_html: Path = OFFLINE_HTML) -> None:
    expected = {standard_package.name, "playwise-offline.html"}
    with zipfile.ZipFile(path) as package:
        names = safe_zip_members(package)
        if len(names) != len(set(names)):
            raise PackageError(f"{path.name}: complete package contains duplicate entries")
        if set(names) != expected:
            details = ", ".join(sorted(names)) or "<empty>"
            raise PackageError(f"{path.name}: complete package must contain only {', '.join(sorted(expected))}; got {details}")
        embedded_standard = package.read(standard_package.name)
        embedded_html = package.read("playwise-offline.html")
    if embedded_standard != standard_package.read_bytes():
        raise PackageError(f"{path.name}: embedded standard package differs from {standard_package.name}")
    if embedded_html != offline_html.read_bytes():
        raise PackageError(f"{path.name}: embedded playwise-offline.html differs from the generated standalone page")


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
    for marker in FORBIDDEN_EDEN_MARKERS:
        if marker in data:
            raise PackageError(f"{path.name}: sysmodule contains forbidden Eden marker {marker.decode('ascii')}")


def verify_packaged_artifacts(path: Path, manifest: dict) -> None:
    with zipfile.ZipFile(path) as package:
        package_manifest = json.loads(package.read(APP_BUILD).decode("utf-8"))
        if package_manifest != manifest:
            raise PackageError(f"{path.name}: package build.json differs from the generated release manifest")
        expected = {
            f"{CONTENT_ROOT}/exefs.nsp": ROOT / "build" / "switch" / "exefs.nsp",
            "switch/playwise/pctc.nro": ROOT / "build" / "switch" / "pctc.nro",
            "switch/.overlays/playwise.ovl": ROOT / "build" / "switch" / "playwise.ovl",
        }
        for member_name, artifact in expected.items():
            if package.read(member_name) != artifact.read_bytes():
                raise PackageError(f"{path.name}: packaged {member_name} differs from the verified build artifact")


def latest_packages(package_dir: Path) -> dict[str, Path]:
    standard = package_dir / STANDARD_PACKAGE
    complete = package_dir / COMPLETE_PACKAGE
    device_lab = package_dir / DEVICE_LAB_PACKAGE
    missing = [path.name for path in (standard, complete, device_lab) if not path.is_file()]
    if missing:
        raise PackageError(f"missing generated package: {', '.join(missing)}")
    zip_names = {path.name for path in package_dir.glob("*.zip")}
    expected = {STANDARD_PACKAGE, COMPLETE_PACKAGE, DEVICE_LAB_PACKAGE}
    if zip_names != expected:
        raise PackageError("release build must produce exactly the standard, complete and device lab public zips")
    return {"playwise": standard, "complete": complete, "device_lab": device_lab}


def container_command(container_path: str = DEFAULT_CONTAINER_PATH, *, with_eden: bool = False) -> str:
    path = "/opt/devkitpro/devkitA64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    # The emulator NRO is opt-in so the default run keeps its exact artifact set.
    targets = "make test packages"
    if with_eden:
        targets += " eden-test-nro"
    container_script = (
        "export DEVKITPRO=/opt/devkitpro "
        "DEVKITARM=/opt/devkitpro/devkitARM "
        "DEVKITA64=/opt/devkitpro/devkitA64 "
        f"PATH={shlex.quote(path)} "
        f"&& cd {shlex.quote(container_path)} "
        f"&& make clean{' CLEAN_EDEN=1' if with_eden else ''} "
        f"&& {targets}"
    )
    return f"sh -lc {shlex.quote(container_script)}"


def ssh_command(
    host: str = DEFAULT_SSH_HOST,
    port: int = DEFAULT_SSH_PORT,
    user: str = DEFAULT_SSH_USER,
    container_path: str = DEFAULT_CONTAINER_PATH,
    identity: Path | None = None,
    *,
    with_eden: bool = False,
) -> list[str]:
    command = ["ssh", "-p", str(port), "-o", "ConnectTimeout=10"]
    if host in {"127.0.0.1", "localhost", "::1"}:
        # Local development containers can be recreated with a different host key.
        command.extend(["-o", "StrictHostKeyChecking=no", "-o", f"UserKnownHostsFile={os.devnull}"])
    if identity is not None:
        command.extend(["-i", str(identity)])
    command.extend([f"{user}@{host}", container_command(container_path, with_eden=with_eden)])
    return command


def run_container(
    host: str = DEFAULT_SSH_HOST,
    port: int = DEFAULT_SSH_PORT,
    user: str = DEFAULT_SSH_USER,
    container_path: str = DEFAULT_CONTAINER_PATH,
    identity: Path | None = None,
    *,
    with_eden: bool = False,
) -> None:
    process = subprocess.run(
        ssh_command(host, port, user, container_path, identity, with_eden=with_eden),
        cwd=ROOT,
        stdin=None,
    )
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
    *,
    with_eden: bool = False,
) -> None:
    package_dir = ROOT / "build" / "packages"
    device_lab_dir = ROOT / "build" / "device-lab"
    eden_dir = ROOT / "build" / "eden-test"
    clean_package_results(package_dir)
    remove_path(device_lab_dir)
    if with_eden:
        remove_path(eden_dir)
    run_container(host, port, user, container_path, identity, with_eden=with_eden)
    manifest_path = ROOT / "build" / "generated" / "release-manifest.json"
    if not manifest_path.is_file():
        raise PackageError(f"missing generated release manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    packages = latest_packages(package_dir)
    standard_package = packages["playwise"]
    verify_package_zip(standard_package, "playwise", expected_manifest=manifest)
    verify_packaged_artifacts(standard_package, manifest)
    verify_complete_package(packages["complete"], standard_package)
    verify_flat_sysmodule(ROOT / "build" / "switch" / "pctc-sysmodule.bin", manifest, release=True)
    device_lab_package = packages["device_lab"]
    verify_device_lab_zip(device_lab_package)
    with zipfile.ZipFile(device_lab_package) as package:
        lab_manifest = json.loads(package.read("switch/playwise-device-lab/build.json").decode("utf-8"))
    verify_flat_sysmodule(device_lab_dir / "switch" / "pwtl-sysmodule.bin", lab_manifest, release=False)
    if with_eden:
        eden_manifest_path = eden_dir / "generated" / "release-manifest.json"
        if not eden_manifest_path.is_file():
            raise PackageError(f"missing generated Eden manifest: {eden_manifest_path}")
        eden_manifest = json.loads(eden_manifest_path.read_text(encoding="utf-8"))
        verify_eden_nro(eden_dir / EDEN_NRO, eden_manifest)


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
    parser.add_argument(
        "--with-eden",
        action="store_true",
        help="Also build and verify the emulator-only Eden NRO in build/eden-test/.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        build_and_verify(
            args.host,
            args.port,
            args.user,
            args.container_path,
            args.identity,
            with_eden=args.with_eden,
        )
    except (OSError, PackageError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        print(f"FAIL: container packages: {exc}")
        return 1
    print(f"PASS: container packages -> {ROOT / 'build' / 'packages'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
