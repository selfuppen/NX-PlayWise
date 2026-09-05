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
    libtesla = (ROOT / "companion/overlay/vendor/libtesla/include/tesla.hpp").read_text(encoding="utf-8")
    overlay_makefile = (ROOT / "companion/overlay/Makefile").read_text(encoding="utf-8")

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

    print("switch IPC lifecycle contract passed")


if __name__ == "__main__":
    main()
