#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    main_source = "\n".join(
        (ROOT / "companion/nro" / name).read_text(encoding="utf-8")
        for name in (
            "main.c",
            "nro_runtime.c",
            "nro_input_dialogs.c",
            "nro_preferences.c",
            "nro_requests.c",
            "nro_redemption.c",
            "nro_result_poll.c",
            "nro_setup.c",
            "nro_security.c",
            "nro_pairing.c",
            "nro_history.c",
            "nro_policy_requests.c",
            "nro_support.c",
            "nro_actions.c",
            "nro_action_dispatch.c",
            "nro_plan_actions.c",
            "nro_input.c",
            "nro_touch_input.c",
        )
    )
    graphics = "\n".join(
        (ROOT / "companion/nro" / name).read_text(encoding="utf-8")
        for name in (
            "ui_graphics.c",
            "ui_render_primitives.c",
            "ui_render_text.c",
            "ui_render_core.c",
            "ui_render_components.c",
            "ui_render_action_card.c",
            "ui_render_child.c",
            "ui_render_setup.c",
            "ui_render_plan.c",
            "ui_render_pages.c",
            "ui_render_dialogs.c",
            "ui_render_dialog_policy.c",
            "ui_render_dialog_input.c",
            "ui_render_overlay_account.c",
            "ui_render_overlay_plan.c",
            "ui_render_overlay_support.c",
            "ui_render_overlays.c",
        )
    )
    core = "\n".join(
        (ROOT / "sysmodule" / name).read_text(encoding="utf-8")
        for name in (
            "sysmodule_storage.c",
            "sysmodule_audit.c",
            "sysmodule_history.c",
            "sysmodule_results.c",
            "sysmodule_control.c",
            "sysmodule_setup.c",
            "sysmodule_requests.c",
            "sysmodule_request_grants.c",
            "sysmodule_request_recovery.c",
            "sysmodule_core.c",
        )
    )

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
