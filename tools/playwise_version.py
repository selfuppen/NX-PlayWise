#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER_PATTERN = re.compile(r'^#define\s+PLAYWISE_VERSION\s+"([^"]+)"\s*$', re.MULTILINE)
MAKE_PATTERN = re.compile(r"^PLAYWISE_VERSION\s*:?=\s*(\S+)\s*$", re.MULTILINE)
VERSION_PATTERN = re.compile(r"^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?$")


def _read_match(path: Path, pattern: re.Pattern[str]) -> str:
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1)
    raise ValueError(f"missing PLAYWISE_VERSION in {path}")


def read_playwise_version(root: Path = ROOT) -> str:
    header_version = _read_match(root / "common" / "version.h", HEADER_PATTERN)
    make_version = _read_match(root / "common" / "version.mk", MAKE_PATTERN)
    if header_version != make_version:
        raise ValueError(
            "PlayWise version mismatch: "
            f"common/version.h={header_version!r}, common/version.mk={make_version!r}"
        )
    return header_version


def validate_playwise_version(version: str) -> str:
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"invalid PlayWise version: {version!r}")
    return version


def write_playwise_version(version: str, root: Path = ROOT) -> None:
    version = validate_playwise_version(version)
    header_path = root / "common" / "version.h"
    make_path = root / "common" / "version.mk"

    header = header_path.read_text(encoding="utf-8")
    make = make_path.read_text(encoding="utf-8")
    header, header_count = HEADER_PATTERN.subn(f'#define PLAYWISE_VERSION "{version}"', header)
    make, make_count = MAKE_PATTERN.subn(f"PLAYWISE_VERSION := {version}", make)
    if header_count != 1 or make_count != 1:
        raise ValueError("version sources must each contain exactly one PLAYWISE_VERSION")

    header_path.write_text(header, encoding="utf-8", newline="\n")
    make_path.write_text(make, encoding="utf-8", newline="\n")
