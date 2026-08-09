#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER_PATTERN = re.compile(r'^#define\s+PLAYWISE_VERSION\s+"([^"]+)"\s*$')
MAKE_PATTERN = re.compile(r"^PLAYWISE_VERSION\s*:?=\s*(\S+)\s*$")


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
