#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    main_source = (ROOT / "companion/nro/main.c").read_text(encoding="utf-8")
    graphics = (ROOT / "companion/nro/ui_graphics.c").read_text(encoding="utf-8")
    core = (ROOT / "sysmodule/sysmodule_core.c").read_text(encoding="utf-8")

    eden_line = next(line for line in makefile.splitlines() if "DEFINES=-DPLAYWISE_EDEN" in line)
    require("eden-test-nro" not in makefile.split("packages:", 1)[-1].splitlines()[0],
            "standard package target must not depend on the Eden NRO")
    require("EDEN_BUILD=1" in eden_line, "Eden target must compile its in-process backend")
    require("PTC_UI_OVERLAY_EDEN_BEDTIME" in main_source and "submit_eden_bedtime_policy" in main_source,
            "Eden NRO must expose bedtime configuration and submission")
    require("模拟就寝时间" in graphics and "跳过当前模拟窗口" in graphics,
            "Eden bedtime UI must expose configuration and recovery")
    require("#ifdef PLAYWISE_EDEN" in core and "next.unverified_overlay_risk_accepted = true" in core,
            "risk bypass must remain explicitly scoped to the Eden build")
    require("version->valueint != 1 && version->valueint != 2" in main_source,
            "NRO rule drafts must reload bedtime-capable version 2 rules")
    print("PASS: Eden bedtime profile checks")


if __name__ == "__main__":
    main()
