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
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    require(
        "elseif (NOT USE_SDL2 OR NOT USE_EGL OR NOT USE_GL3 OR USE_GL2 OR USE_GLES2 OR USE_GLES3)" in cmake,
        "the default Switch PoC must require the SDL2/EGL/OpenGL 4.3 core backend",
    )
    require(
        "if (USE_DEKO3D)\n    gen_dksh" in cmake,
        "deko3d shaders must not be generated for the OpenGL PoC",
    )
    default_target = makefile.split("borealis-poc-nro:", 1)[1].split("borealis-poc-deko3d-nro:", 1)[0]
    require(
        "-DUSE_DEKO3D=OFF" in default_target
        and "-DUSE_SDL2=ON" in default_target
        and "-DUSE_EGL=ON" in default_target
        and "-DUSE_GL3=ON" in default_target
        and "-DUSE_GL2=OFF" in default_target
        and "-DUSE_GLES2=OFF" in default_target
        and "-DUSE_GLES3=OFF" in default_target,
        "borealis-poc-nro must build the stability-first OpenGL variant",
    )
    deko3d_target = makefile.split("borealis-poc-deko3d-nro:", 1)[1].split("borealis-poc-clean:", 1)[0]
    require(
        "-DUSE_DEKO3D=ON" in deko3d_target
        and "build/borealis-poc-deko3d" in deko3d_target,
        "deko3d must remain available only as an isolated comparison target",
    )
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
