#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
import os
import re
import subprocess

from tools.build.playwise_version import read_playwise_version


ROOT = Path(__file__).resolve().parents[2]
VERSION = read_playwise_version(ROOT)
LIBTESLA_UPSTREAM = ROOT / "companion" / "overlay" / "vendor" / "libtesla" / "UPSTREAM.txt"


def command_output(command: list[str], fallback: str = "unknown") -> str:
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=True)
    except (OSError, subprocess.SubprocessError):
        return fallback
    lines = result.stdout.strip().splitlines()
    return lines[0] if lines else fallback


def git_commit() -> str:
    return command_output(["git", "rev-parse", "HEAD"])


def git_tracked_dirty() -> bool:
    command = ["git"]
    autocrlf = os.environ.get("PLAYWISE_GIT_AUTOCRLF")
    if autocrlf is not None:
        if autocrlf not in ("true", "false", "input"):
            raise ValueError("invalid host Git newline policy")
        command.extend(["-c", f"core.autocrlf={autocrlf}"])
    command.extend(["diff", "--quiet", "HEAD", "--"])
    try:
        result = subprocess.run(
            command,
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
    except OSError:
        # A missing or unreadable Git checkout must never be presented as a
        # clean release candidate.
        return True
    return result.returncode != 0


def source_fingerprint() -> str:
    """Hash build inputs, including new non-ignored sources, without timestamps."""
    result = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
                            cwd=ROOT, capture_output=True, check=True)
    digest = hashlib.sha256()
    for name in sorted(set(result.stdout.split(b"\0")) - {b""}):
        path = ROOT / os.fsdecode(name)
        digest.update(len(name).to_bytes(8, "big"))
        digest.update(name)
        if not path.exists():
            digest.update(b"missing\0")
            continue
        if path.is_symlink():
            digest.update(b"symlink\0" + os.fsencode(os.readlink(path)))
            continue
        digest.update(b"file\0")
        digest.update(path.stat().st_size.to_bytes(8, "big"))
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    return digest.hexdigest()


def toolchain_identity() -> str:
    compiler = Path(os.environ.get("DEVKITA64", "")) / "bin" / "aarch64-none-elf-gcc"
    if os.name == "nt":
        compiler = compiler.with_suffix(".exe")
    return command_output([str(compiler), "--version"]) if compiler.is_file() else "unknown"


def libnx_identity() -> str:
    devkitpro = Path(os.environ.get("DEVKITPRO", ""))
    for pacman in (Path("pacman"), devkitpro / "pacman" / "bin" / "pacman"):
        package = command_output([str(pacman), "-Q", "libnx"])
        if re.fullmatch(r"libnx\s+\S+", package):
            return package
    package_db = devkitpro / "pacman" / "var" / "lib" / "pacman" / "local"
    for description in sorted(package_db.glob("libnx-*/desc"), reverse=True):
        lines = description.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines[:-1]):
            if line == "%VERSION%" and lines[index + 1].strip():
                return f"libnx {lines[index + 1].strip()}"
    for pkgconfig in (
        devkitpro / "libnx" / "lib" / "pkgconfig" / "libnx.pc",
        devkitpro / "libnx" / "share" / "pkgconfig" / "libnx.pc",
    ):
        if not pkgconfig.is_file():
            continue
        for line in pkgconfig.read_text(encoding="utf-8").splitlines():
            if line.startswith("Version:") and line.partition(":")[2].strip():
                return f"libnx {line.partition(':')[2].strip()}"
    return "unknown"


def libtesla_commit() -> str:
    if not LIBTESLA_UPSTREAM.is_file():
        return "unknown"
    for token in LIBTESLA_UPSTREAM.read_text(encoding="utf-8").replace("=", " ").split():
        if len(token) == 40 and all(ch in "0123456789abcdefABCDEF" for ch in token):
            return token.lower()
    return "unknown"


def collect_build_identity() -> dict:
    # Shared only within this invocation; never trust a previous run's snapshot.
    return {
        "commit": git_commit(),
        "libnx": libnx_identity(),
        "compiler": toolchain_identity(),
        "source_hash": source_fingerprint(),
        "source_dirty": git_tracked_dirty(),
        "libtesla": libtesla_commit(),
        "image": os.environ.get("PLAYWISE_BUILD_IMAGE", "unknown"),
        "image_digest": os.environ.get("PLAYWISE_BUILD_IMAGE_DIGEST", "unknown"),
    }


def make_manifest(profile: str, *, build_identity: dict | None = None) -> dict:
    snapshot = collect_build_identity() if build_identity is None else build_identity
    commit = snapshot["commit"]
    libnx = snapshot["libnx"]
    compiler = snapshot["compiler"]
    source_hash = snapshot["source_hash"]
    build_image = snapshot["image"]
    build_image_digest = snapshot["image_digest"]
    manual_verified = profile == "release" and os.environ.get("PLAYWISE_MANUAL_DEVICE_VERIFIED") == "1"
    device_model = os.environ.get("PLAYWISE_VERIFIED_MODEL", "").strip()
    hos = os.environ.get("PLAYWISE_VERIFIED_HOS", "").strip()
    atmosphere = os.environ.get("PLAYWISE_VERIFIED_ATMOSPHERE", "").strip()
    if manual_verified and not all((device_model, hos, atmosphere)):
        raise ValueError("manual device verification requires model, HOS and Atmosphère versions")
    data = {
        "schema_version": 1,
        "playwise_version": VERSION,
        "commit": commit,
        "release_id": f"playwise-{VERSION}+{commit[:12]}",
        "profile": profile,
        "protocol_version": 1,
        "recovery_version": 1,
        "pctl_layout_version": 1,
        "build": {
            "devkitpro": compiler,
            "libnx": libnx,
            "libtesla_commit": snapshot["libtesla"],
            "container_image": build_image,
            "container_image_digest": build_image_digest,
            "source_dirty": snapshot["source_dirty"],
            "source_fingerprint": source_hash,
        },
        "qualification": {
            "status": "manual_verified" if manual_verified else "pending",
            "artifact_binding": "embedded-manifest" if manual_verified else "detached-sha256",
        },
        "verified_environment": {
            "model": device_model if manual_verified else "Nintendo Switch OLED",
            "hos": hos if manual_verified else "22.5.0",
            "atmosphere": atmosphere if manual_verified else "1.11.2",
            "result": "manual_verified" if manual_verified else "pending",
        },
    }
    # Keep all profiles in the same source/toolchain generation. Exact installed
    # bytes are still bound separately by package-artifacts.json and Zip SHA-256.
    identity = {"source": source_hash, "compiler": compiler, "libnx": libnx,
                "libtesla": data["build"]["libtesla_commit"], "image": build_image_digest}
    generation = hashlib.sha256(json.dumps(identity, sort_keys=True).encode("utf-8")).hexdigest()[:16]
    data["release_id"] += "." + generation
    if manual_verified:
        data["qualification"].update({"method": "manual", "scope": "main_features"})
    if "PLAYWISE_GIT_AUTOCRLF" in os.environ:
        data["build"]["git_autocrlf"] = os.environ["PLAYWISE_GIT_AUTOCRLF"]
    if profile == "release":
        data["hot_reload_events_version"] = 1
        data["capture_restart_version"] = 1
        from tools.qualification.pctl_usage_qualification import CURRENT_CONTRACT
        data["usage_contract"] = CURRENT_CONTRACT
        data["ui_status_trace_version"] = 1
        data["nro_overlay_input_owner_version"] = 1
    return data


def c_string(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def write_text_if_changed(path: Path, text: str) -> None:
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_header(path: Path, data: dict) -> None:
    compact = json.dumps(data, ensure_ascii=True, separators=(",", ":"))
    lines = [
        "#ifndef PLAYWISE_GENERATED_RELEASE_MANIFEST_H",
        "#define PLAYWISE_GENERATED_RELEASE_MANIFEST_H",
        "",
        f'#define PLAYWISE_BUILD_VERSION "{c_string(data["playwise_version"])}"',
        f'#define PLAYWISE_BUILD_COMMIT "{c_string(data["commit"])}"',
        f'#define PLAYWISE_BUILD_RELEASE_ID "{c_string(data["release_id"])}"',
        f'#define PLAYWISE_BUILD_PROFILE "{c_string(data["profile"])}"',
        f'#define PLAYWISE_RELEASE_MANIFEST_JSON "{c_string(compact)}"',
        "",
        "#endif",
        "",
    ]
    write_text_if_changed(path, "\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate PlayWise build manifest assets.")
    profiles = ("release", "device-lab", "eden-test")
    parser.add_argument("--profile", choices=profiles)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--output", nargs=3, action="append", metavar=("PROFILE", "JSON", "HEADER"),
                        help="Repeat to generate selected profiles from one fresh build identity.")
    args = parser.parse_args()
    if args.output:
        if args.profile or args.json or args.header:
            parser.error("--output cannot be combined with --profile/--json/--header")
        outputs = [(profile, Path(json_path), Path(header)) for profile, json_path, header in args.output]
    else:
        if not args.json or not args.header:
            parser.error("--json and --header are required without --output")
        outputs = [(args.profile or "release", args.json, args.header)]
    if any(profile not in profiles for profile, _, _ in outputs):
        parser.error("unknown output profile")
    paths = [path.resolve() for _, json_path, header in outputs for path in (json_path, header)]
    if len(paths) != len(set(paths)):
        parser.error("manifest output paths must be distinct")
    identity = collect_build_identity()
    # Validate the entire selection before writing any candidate metadata.
    manifests = [(make_manifest(profile, build_identity=identity), json_path, header)
                 for profile, json_path, header in outputs]
    if any(data["profile"] in ("release", "device-lab") and data["build"]["libnx"] == "unknown"
           for data, _, _ in manifests):
        raise SystemExit("cannot create a release candidate: installed libnx identity is unknown")
    for data, json_path, header in manifests:
        write_text_if_changed(json_path, json.dumps(data, ensure_ascii=False, indent=2) + "\n")
        write_header(header, data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
