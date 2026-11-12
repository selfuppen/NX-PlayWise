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
    sysmodule = (ROOT / "sysmodule/source/main.c").read_text(encoding="utf-8")
    reason_notification = (ROOT / "sysmodule/reason_notification.h").read_text(encoding="utf-8")
    hidden_poll = (ROOT / "companion/overlay/hidden_poll.h").read_text(encoding="utf-8")
    power_ledger = (ROOT / "sysmodule/power_ledger.h").read_text(encoding="utf-8")
    nro_main = (ROOT / "companion/nro/main.c").read_text(encoding="utf-8")
    nro_source = "\n".join(
        (ROOT / "companion/nro" / name).read_text(encoding="utf-8")
        for name in (
            "main.c",
            "nro_runtime.c",
            "nro_requests.c",
            "nro_setup.c",
            "nro_security.c",
            "nro_pairing.c",
            "nro_history.c",
            "nro_policy_requests.c",
            "nro_support.c",
            "nro_actions.c",
            "nro_page_input.c",
            "nro_input.c",
            "nro_overlay_plan_input.c",
            "nro_overlay_account_input.c",
            "nro_touch_input.c",
            "nro_app_internal.h",
        )
    )

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
    require("detail::GlyphCache<Glyph, 512, 256 * 1024> m_glyphCache;" in libtesla,
            "Overlay bitmap cache must have fixed metadata and a 256 KiB budget")
    require("std::unordered_map<u64, Glyph>" not in libtesla,
            "Overlay glyphs must not accumulate in an unbounded map")
    require("stbtt_FreeBitmap(bitmap, nullptr)" in libtesla and
            "this->m_glyphCache.clear();" in libtesla,
            "Overlay glyph ownership and renderer exit must release bitmaps")
    require("TESLA_DISABLE_EMOJI_FONT" in overlay,
            "PlayWise must not load unused external emoji fonts on its small heap")
    require("std::atomic<bool> running" in libtesla and
            "std::atomic<bool> overlayOpen" in libtesla,
            "cross-thread Overlay lifecycle flags must be atomic")
    poller = libtesla[libtesla.index("static void backgroundEventPoller"):libtesla.index("template<typename G, typename ...Args>\n    std::unique_ptr<tsl::Gui>& changeTo")]
    require("Overlay::get()->hide()" not in poller and "shData->hideRequested = true;" in poller,
            "the input thread must signal hide instead of modifying GUI state")
    loop = libtesla[libtesla.index("static inline int loop(int argc, char** argv)"):]
    require("detail::inputPollNs(shData->overlayOpen.load(), shData->keysHeld != 0)" in poller,
            "idle input throttling must preserve visible and held-button sampling")
    require("detail::hiddenPollNs(shData.automaticOpenBlocked.load(), overlay->hiddenPollInterval())" in loop,
            "HOME/POWER blocked hidden UI must wait for the manual combo event")
    require("ptc_sysmodule_idle_wait_ms(wait_ms, fast_poll)" in sysmodule and
            "usage_evidence.active ||" in sysmodule and
            "ptc_reason_notification_busy(&reason_notification)" in sysmodule and
            "reason_enabled ||" not in sysmodule,
            "only capture, pending material and unhandled restriction changes may force fast polling")
    require("!n->blocked && (n->pending || n->detail_pending || n->event.dirty)" in reason_notification,
            "notification fast polling must cover pending toasts, detail offers and unhandled changes only")
    # Switch-only wiring that Host C tests cannot compile: the production loop must
    # enable sampled-state coalescing, flush it before the handoff freezes writes and
    # after the loop ends, and keep the freeze ledger free of periodic I/O.
    require("sysmodule.state_persist_interval_s = 30;" in sysmodule,
            "standard backend must coalesce sampling-only state writes")
    quiesce_pos = sysmodule.index("sysmodule->quiescing = true;")
    require(sysmodule.rfind("ptc_sysmodule_flush_state(sysmodule)", 0, quiesce_pos) != -1 and
            sysmodule.index("ptc_sysmodule_flush_state(&sysmodule)") > sysmodule.index("while (!exit_for_handoff)"),
            "coalesced samples must be flushed before quiescing and after the control loop")
    lab_guard = sysmodule.rfind("#ifndef PLAYWISE_DEVICE_LAB", 0, sysmodule.index("sysmodule.state_persist_interval_s = 30;"))
    require(lab_guard != -1 and "#endif" not in sysmodule[lab_guard:sysmodule.index("sysmodule.state_persist_interval_s = 30;")],
            "Device Lab must keep immediate state persistence")
    require("ptc_power_ledger_observe(&power_ledger, reason_ns, reason_clock.unix_seconds, wait_ms, due)" in sysmodule and
            "!reason_stopping && ptc_power_ledger_due(&power_ledger)" in sysmodule and
            '"/support/power-ledger.json"' in sysmodule and
            "ptc_power_ledger_failed(&power_ledger)" in sysmodule,
            "freeze ledger must reuse the loop clocks, stay silent while quiescing and bound write retries")
    require("PTC_POWER_LEDGER_GAP_NS UINT64_C(15000000000)" in power_ledger and
            "PTC_POWER_LEDGER_SETTLE_NS UINT64_C(120000000000)" in power_ledger,
            "freeze ledger thresholds are part of the documented diagnostic contract")
    require("ptc_overlay_hidden_poll_ns(automatic_enabled_, automatic_failures_)" in overlay and
            "ms + PTC_OVERLAY_HIDDEN_CONFIG_MS" in overlay and
            "ptc_overlay_hidden_runtime_next_ms(ms, runtime_known_)" in overlay,
            "hidden Overlay must use the shared cadence and cache the backend identity")
    # An unanswered detail offer is dropped once its observation ages, so the
    # presence lease must stay short and be renewed only from neutral input.
    hidden = overlay[overlay.index("bool onHiddenPoll(bool neutral) override"):overlay.index("void onFrameRendered() override")]
    require("ready.expires_ms = ms + 1500;" in hidden and "next_ready_ms_ = ms + 1000;" in hidden,
            "hidden Overlay must keep the 1.5s presence lease renewed every second")
    require(hidden.index("ms >= next_config_ms_") < hidden.index("!automatic_enabled_ || !neutral") <
            hidden.index("ptc_reason_detail_runtime(") < hidden.index('ptc_reason_detail_write(s, APP_ROOT, "ready"'),
            "preference checks ignore input, but identity reads and the lease need neutral input")
    require("UINT64_C(250000000) : UINT64_C(5000000000)" in hidden_poll,
            "hidden Overlay polls every five seconds while reason notifications are off")
    require(loop.index("eventCreate(&shData.comboEvent") < loop.index("threadStart(&backgroundThread)"),
            "combo event must exist before the input thread starts")
    require(loop.index("threadWaitForExit(&backgroundThread)") < loop.index("eventClose(&shData.comboEvent)"),
            "combo event must stay alive until the input thread exits")
    require("standard_backend_expected()" in nro_source,
            "standard NRO must skip IPC when Device Lab has disabled its boot flag")
    require("pmshellTerminateProcess" not in hot_reload and "pmshellTerminateProcess" not in nro_source,
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
    disable_failure = hot_reload[hot_reload.index("void ptc_hot_reload_confirm_ipc_closed"):hot_reload.index("void ptc_hot_reload_tick")]
    require("controller->boot_disabled = exists(BOOT_FLAG_BACKUP_PATH)" in disable_failure,
            "a failed commit after flag rename must still restore the original flag")
    recover_pos = nro_main.index("ptc_hot_reload_recover_startup(&ui.hot_reload);")
    expected_pos = nro_main.index("backend_expected = standard_backend_expected()")
    require(recover_pos < expected_pos,
            "standard NRO must restore an interrupted boot flag before deciding whether IPC is expected")
    recovery = hot_reload[hot_reload.index("void ptc_hot_reload_recover_startup"):hot_reload.index("void ptc_hot_reload_inspect")]
    require("pmshellLaunchProgram" not in recovery and "PTC_HOT_RELOAD_PHASE_WAIT_SOURCE_EXIT" in recovery
            and "verify_artifacts" in recovery and "source_boot_id" in recovery,
            "startup recovery must reuse bounded absence/ready checks and validate the frozen target")
    require("ui.hot_reload.status != PTC_HOT_RELOAD_RUNNING" in nro_main and
            "write_runtime_ready(&sysmodule, ipc_available)" in sysmodule,
            "startup must not connect before recovery and runtime must expose failed IPC registration")
    require("ui->model.view != PTC_UI_PARENT" in nro_source and
            "PTC_UI_OPERATION_HOT_RELOAD" in nro_source,
            "hot reload confirmation must remain reachable only after entering the PIN-protected parent area")
    runtime = (ROOT / "companion/nro/nro_runtime.c").read_text(encoding="utf-8")
    failed_branch = runtime[runtime.index("} else if (ui->hot_reload.phase == PTC_HOT_RELOAD_PHASE_FAILED"):]
    require("ptc_switch_ipc_client_probe" not in failed_branch,
            "failed handover must never wait for an absent service to register")
    require("ptc_companion_transport_set_suspended(&ui->transport, true)" in failed_branch,
            "failed handover must keep both IPC and file submissions suspended")
    require('"/handover/events/%s.jsonl"' in hot_reload and '"runtime_ready"' in hot_reload,
            "controller must retain an append-only transaction trace after journal cleanup")
    read_status_start = pctl_adapter.index("static PtcErrorCode switch_read_status")
    read_status_end = pctl_adapter.index("static PtcErrorCode switch_backup", read_status_start)
    read_status = pctl_adapter[read_status_start:read_status_end]
    require(read_status.count("PTC_PCTL_CMD_IS_RESTRICTED_BY_PLAY_TIMER") == 2,
            "Switch status must try restricted-now on both pctl and pctl:s")
    require("if (!out->restricted_now_available)" in read_status and
            "&settings_session.service" in read_status,
            "pctl:s restricted-now fallback must run only when the pctl query is unavailable")
    require("volatile bool status_refresh_pending;" in nro_source,
            "NRO state must track pending status refresh across sleep and focus transitions")
    require("ui->status_refresh_pending = true;" in nro_source,
            "NRO applet hook must flag status refresh upon resume or returning to focus")
    require("trigger_resume_status_refresh(&ui, &background_poll_elapsed_ms);" in nro_main,
            "NRO main loop must actively trigger status refresh and poll when resuming from sleep")

    print("switch IPC lifecycle contract passed")


if __name__ == "__main__":
    main()
