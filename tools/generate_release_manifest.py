#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.1.2"
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


def toolchain_identity() -> str:
    compiler = Path(os.environ.get("DEVKITA64", "")) / "bin" / "aarch64-none-elf-gcc"
    if os.name == "nt":
        compiler = compiler.with_suffix(".exe")
    return command_output([str(compiler), "--version"]) if compiler.is_file() else "unknown"


def libnx_identity() -> str:
    version_header = Path(os.environ.get("DEVKITPRO", "")) / "libnx" / "include" / "switch" / "version.h"
    if version_header.is_file():
        values: dict[str, str] = {}
        for line in version_header.read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[0] == "#define" and parts[1].startswith("LIBNX_VERSION_"):
                values[parts[1]] = parts[2]
        keys = ("LIBNX_VERSION_MAJOR", "LIBNX_VERSION_MINOR", "LIBNX_VERSION_MICRO")
        if all(key in values for key in keys):
            return ".".join(values[key] for key in keys)
    return "unknown"


def libtesla_commit() -> str:
    if not LIBTESLA_UPSTREAM.is_file():
        return "unknown"
    for token in LIBTESLA_UPSTREAM.read_text(encoding="utf-8").replace("=", " ").split():
        if len(token) == 40 and all(ch in "0123456789abcdefABCDEF" for ch in token):
            return token.lower()
    return "unknown"


def make_manifest(profile: str) -> dict:
    commit = git_commit()
    return {
        "schema_version": 1,
        "playwise_version": VERSION,
        "commit": commit,
        "release_id": f"playwise-{VERSION}+{commit[:12]}",
        "profile": profile,
        "protocol_version": 1,
        "recovery_version": 1,
        "pctl_layout_version": 1,
        "build": {
            "devkitpro": toolchain_identity(),
            "libnx": libnx_identity(),
            "libtesla_commit": libtesla_commit(),
        },
        "verified_environment": {
            "model": "Nintendo Switch OLED",
            "hos": "22.5.0",
            "atmosphere": "1.11.2",
            "qualification_date": "2026-08-07",
            "result": "requires-requalification-for-final-artifact",
        },
    }


def c_string(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


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
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate PlayWise build manifest assets.")
    parser.add_argument("--profile", choices=("release", "device-lab"), default="release")
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    data = make_manifest(args.profile)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    write_header(args.header, data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
