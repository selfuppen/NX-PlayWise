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
    require("PTC_UI_OVERLAY_BEDTIME" in main_source and "submit_bedtime_policy" in main_source,
            "standard and Eden NROs must expose bedtime configuration and submission")
    require("submit_bedtime_confirmation" in main_source and
            "ptc_companion_transport_submit_confirm_bedtime_requirements" in main_source,
            "standard NRO bedtime enablement must record the official PCTL confirmation")
    require("就寝时间" in graphics and "仅可从 Overlay" in graphics,
            "bedtime UI must explain the lockout and recovery surface")
    require("#ifdef PLAYWISE_EDEN" in core and "next.unverified_overlay_risk_accepted = true" in core,
            "risk bypass must remain explicitly scoped to the Eden build")
    require("version->valueint != 1 && version->valueint != 2" in main_source,
            "NRO rule drafts must reload bedtime-capable version 2 rules")
    package_source = (ROOT / "tools/package_sdmc.py").read_text(encoding="utf-8")
    package_gate = (ROOT / "tools/package_remote.py").read_text(encoding="utf-8")
    require('"bedtime_enabled": False' in package_source and '"version": 2' in package_source,
            "new installs must seed a disabled bedtime-capable rules schema")
    require('b"bedtime"' not in package_gate.split("FORBIDDEN_RELEASE_MARKERS", 1)[1].split(")", 1)[0],
            "the Release gate must not reject the standard bedtime capability")
    print("PASS: Eden bedtime profile checks")


if __name__ == "__main__":
    main()
