#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    forbidden = [
        ROOT / "sysmodule/source/main.c",
        ROOT / "platform/switch/time_provider.c",
        ROOT / "platform/switch/pctl_adapter.c",
        ROOT / "companion/overlay/source/main.cpp",
        ROOT / "companion/overlay/source/render_types.hpp",
    ]
    for path in forbidden:
        text = path.read_text(encoding="utf-8")
        assert "timeInitialize(" not in text, (
            f"{path.relative_to(ROOT)} must not retain libnx timeInitialize() (6 time:u sessions + shmem)")
        assert "timeExit(" not in text, (
            f"{path.relative_to(ROOT)} must use ptc_switch_time_* instead of libnx timeExit()")

    client = (ROOT / "platform/switch/time_client.c").read_text(encoding="utf-8")
    assert '"time:u"' in client
    assert "timeInitialize(" not in client
    assert "ptc_switch_time_init(" in (ROOT / "sysmodule/source/main.c").read_text(encoding="utf-8")
    assert "ptc_switch_time_exit(" in (ROOT / "sysmodule/source/main.c").read_text(encoding="utf-8")
    assert "ptc_switch_time_read_local_once(" in (
        ROOT / "companion/overlay/source/render_types.hpp").read_text(encoding="utf-8")
    assert "time_client.c" in (ROOT / "companion/overlay/Makefile").read_text(encoding="utf-8")
    print("PASS: sysmodule and overlay use minimal Switch time_client without retaining libnx timeInitialize()")


if __name__ == "__main__":
    main()
