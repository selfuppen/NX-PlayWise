#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    cmake = (ROOT / "experiments" / "borealis_poc" / "CMakeLists.txt").read_text(encoding="utf-8")
    platform = (
        ROOT
        / "third_party"
        / "borealis"
        / "library"
        / "lib"
        / "platforms"
        / "switch"
        / "switch_platform.cpp"
    ).read_text(encoding="utf-8")
    video = (
        ROOT
        / "third_party"
        / "borealis"
        / "library"
        / "lib"
        / "platforms"
        / "switch"
        / "switch_video.cpp"
    ).read_text(encoding="utf-8")
    upstream = (ROOT / "third_party" / "borealis" / "UPSTREAM.txt").read_text(encoding="utf-8")

    require("USE_DEKO3D" in cmake, "the Switch PoC must keep its pinned deko3d backend")
    require(
        "BOREALIS_SKIP_WIRELESS_PRIORITY" in cmake
        and "#ifndef BOREALIS_SKIP_WIRELESS_PRIORITY" in platform,
        "the offline PoC must not depend on Eden's unimplemented wireless-priority command",
    )
    require(
        ".setFormat(DkImageFormat_Z24S8)" in video
        and ".setFormat(DkImageFormat_S8)" not in video,
        "the PoC must use the emulator-compatible combined depth/stencil format",
    )
    require(
        "5f08b286f3df737f3321d2247a6fe633fcead03c" in upstream,
        "the Borealis source commit must remain explicit and pinned",
    )
    require("Local integration patch:" in upstream, "vendored Borealis differences must be recorded")
    print("Borealis PoC source contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
