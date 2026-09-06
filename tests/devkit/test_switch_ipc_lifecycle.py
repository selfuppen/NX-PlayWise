#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    header = (ROOT / "companion/switch_ipc_client.h").read_text(encoding="utf-8")
    client = (ROOT / "companion/switch_ipc_client.c").read_text(encoding="utf-8")
    bridge = (ROOT / "companion/overlay/bridge.c").read_text(encoding="utf-8")
    overlay = (ROOT / "companion/overlay/source/main.cpp").read_text(encoding="utf-8")
    lab_nro = (ROOT / "device_lab/nro/main.c").read_text(encoding="utf-8")
    lab_overlay = (ROOT / "device_lab/overlay/source/main.cpp").read_text(encoding="utf-8")
    pctl_adapter = (ROOT / "platform/switch/pctl_adapter.c").read_text(encoding="utf-8")
    libtesla = (ROOT / "companion/overlay/vendor/libtesla/include/tesla.hpp").read_text(encoding="utf-8")
    overlay_makefile = (ROOT / "companion/overlay/Makefile").read_text(encoding="utf-8")
    hot_reload = (ROOT / "companion/nro/hot_reload.c").read_text(encoding="utf-8")
    nro_main = (ROOT / "companion/nro/main.c").read_text(encoding="utf-8")

    require("bool sm_initialized;" in header, "Switch IPC client must track its retained SM session")
    require("rc = smInitialize();" in client, "Switch IPC client init must retain an SM session")
    require("client->sm_initialized = true;" in client, "successful SM initialization must be recorded")
    require("if (!client || !client->sm_initialized) return false;" in client,
            "IPC connect must not call smGetService without a retained SM session")
    require("if (client->sm_initialized)" in client and "smExit();" in client,
            "Switch IPC client exit must release its retained SM session")
    require("ptc_companion_transport_cancel(&bridge->transport);" in bridge,
            "Overlay bridge exit must close any outstanding IPC wait event")
    require("ptc_switch_ipc_client_exit(&bridge->ipc);" in bridge,
            "Overlay bridge exit must release the service and SM session")
    exit_pos = overlay.index("ptc_overlay_bridge_exit(&bridge_);")
    unmount_pos = overlay.index('fsdevUnmountDevice("sdmc");', exit_pos)
    require(exit_pos < unmount_pos, "Overlay IPC resources must close before SD is unmounted")
    require("fsdevMountSdmc()" not in lab_nro,
            "Device Lab NRO must use the SD mount provided by the default libnx runtime")
    require('fsdevUnmountDevice("sdmc")' not in lab_nro,
            "Device Lab NRO must leave the default libnx SD mount to runtime teardown")
    require("smInitialize()" not in lab_nro and "smExit()" not in lab_nro,
            "Device Lab NRO must use the SM lifecycle provided by the default libnx runtime")
    require("smGetService" not in lab_nro,
            "Device Lab NRO must never synchronously query the optional pwtl:u service")
    require("DEVICE_LAB_FLAGS_DIR" in lab_nro and "STANDARD_FLAGS_DIR" in lab_nro,
            "Device Lab NRO must materialize both boot-flag parent directories before switching")
    require("hidInitializeTouchScreen();" in lab_nro,
            "Device Lab NRO must activate touch before reading touch states")
    require("ptc_switch_ipc_client_init" not in lab_overlay and
            "ptc_switch_ipc_backend" not in lab_overlay,
            "Device Lab Overlay startup must use the durable SD queue without retaining pwtl:u")
    require("nullptr, nullptr" in lab_overlay,
            "Device Lab Overlay transport must explicitly select the SD queue")
    require("char request[1024];" in lab_overlay and
            "static_cast<std::size_t>(written) >= sizeof(request)" in lab_overlay,
            "Device Lab Overlay must reject a request instead of submitting truncated JSON")
    require("for (u16 i = this->m_focusedIndex; i-- > 0;)" in libtesla,
            "vendored libtesla must traverse upward without unsigned-index underflow")
    require("-Wno-type-limits" not in overlay_makefile,
            "the standard Overlay build must not hide unsigned-index diagnostics")
    require("standard_backend_expected()" in (ROOT / "companion/nro/main.c").read_text(encoding="utf-8"),
            "standard NRO must skip IPC when Device Lab has disabled its boot flag")
    require("pmshellTerminateProcess" not in hot_reload and "pmshellTerminateProcess" not in nro_main,
            "standard hot reload must never force-terminate the source sysmodule")
    require("HANDOFF_TIMEOUT_NS UINT64_C(10000000000)" in hot_reload and
            "EXIT_TIMEOUT_NS UINT64_C(10000000000)" in hot_reload and
            "READY_TIMEOUT_NS UINT64_C(30000000000)" in hot_reload,
            "hot reload must retain the 10s handoff, 10s exit and 30s ready deadlines")
    absent_pos = hot_reload.index("controller->absent_samples >= 3U")
    restore_pos = hot_reload.index("ptc_hot_reload_restore_boot", absent_pos)
    launch_pos = hot_reload.index("launch_target(controller)", restore_pos)
    require(absent_pos < restore_pos < launch_pos,
            "hot reload must prove source exit and restore boot2 before launching the target")
    require("NcmStorageId_None" in hot_reload and "RELEASE_PROGRAM_ID" in hot_reload,
            "hot reload must launch the installed SD title through PM using storage None")
    require("controller->launch_attempts < 2U" in hot_reload,
            "hot reload may retry an absent target only once")
    recover_pos = nro_main.index("ptc_hot_reload_recover_startup(&ui.hot_reload);")
    expected_pos = nro_main.index("backend_expected = standard_backend_expected();")
    require(recover_pos < expected_pos,
            "standard NRO must restore an interrupted boot flag before deciding whether IPC is expected")
    require("ui->model.view != PTC_UI_PARENT" in nro_main and
            "PTC_UI_OPERATION_HOT_RELOAD" in nro_main,
            "hot reload confirmation must remain reachable only after entering the PIN-protected parent area")
    read_status_start = pctl_adapter.index("static PtcErrorCode switch_read_status")
    read_status_end = pctl_adapter.index("static PtcErrorCode switch_backup", read_status_start)
    read_status = pctl_adapter[read_status_start:read_status_end]
    require(read_status.count("PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER") == 2,
            "Switch status must try restricted-now on both pctl and pctl:s")
    require("if (!out->restricted_now_available)" in read_status and
            "&settings_session.service" in read_status,
            "pctl:s restricted-now fallback must run only when the pctl query is unavailable")

    print("switch IPC lifecycle contract passed")


if __name__ == "__main__":
    main()
