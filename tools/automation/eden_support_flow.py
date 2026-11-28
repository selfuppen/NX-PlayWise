"""Targeted support/recovery journey through the real NRO, using isolated Eden data."""
from __future__ import annotations

from pathlib import Path
import shutil

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import decode, digest, read, require, save

SUPPORT_SCENARIOS = ("setup", "support_seed", "support_diagnostics", "support_temp_unlock",
                     "support_disable", "support_restore", "support_reopen")


def verify_diagnostic(value: dict, disabled: bool) -> None:
    require(type(value.get("version")) is int and value["version"] == 1 and
            value.get("redaction") == "credentials-auth-codes-and-nonces-omitted", "diagnostic schema differs")
    runtime = value.get("runtime_snapshot", {})
    require(isinstance(runtime, dict) and runtime.get("status_loaded") is True and runtime.get("disable_flag_present") is disabled and
            type(runtime.get("status_age_seconds")) is int and 0 <= runtime["status_age_seconds"] <= 20,
            "diagnostic runtime snapshot is missing, stale or has wrong disable state")
    forbidden = {"grant_secret", "pin_hash", "pin_salt", "offline_code", "nonce", "used_nonces",
                 "credentials.json", "auth.json"}

    def visit(item: object) -> None:
        if isinstance(item, dict):
            require(not (forbidden & item.keys()), "diagnostic contains sensitive fields")
            for child in item.values():
                visit(child)
        elif isinstance(item, list):
            for child in item:
                visit(child)
        elif isinstance(item, str):
            require(item != "playwise-eden-test-secret-00000001", "diagnostic contains test secret")

    visit(value)


def verify_restored_status(value: dict, baseline: dict) -> None:
    setup, state = value.get("setup", {}), value.get("state", {})
    require(value.get("status") == "ok" and setup.get("phase") == "restored" and
            setup.get("disable_flag_present") is True and setup.get("recovery_active") is False,
            "restored setup/disable/recovery state differs")
    for key in ("limited_today", "blocked_today", "unrestricted_today", "play_timer_enabled"):
        require(type(state.get(key)) is int and state[key] == baseline["state"][key],
                f"restored simulated PCTL projection differs: {key}")
    require(state.get("remaining_available") is True and state.get("played_minutes_available") is True,
            "restored simulated usage is unavailable")
    # Compare observable total, not elapsed balance. Minute truncation can lose
    # one minute; opaque bytes and real PCTL behavior remain lower/device tests.
    require(type(state.get("remaining_minutes")) is int and type(state.get("played_minutes")) is int,
            "restored simulated usage has invalid numeric types")
    total = state["remaining_minutes"] + state["played_minutes"]
    expected = baseline["state"]["remaining_minutes"] + baseline["state"]["played_minutes"]
    require(abs(total - expected) <= 1, "restored simulated total differs from installation baseline")


def verify_support(output: Path) -> None:
    summary_path = output / "summary.json"
    expected_scenarios = SUPPORT_SCENARIOS[1:]
    if summary_path.is_file():
        recorded = read(summary_path).get("scenarios", {})
        if isinstance(recorded, dict) and recorded:
            expected_scenarios = tuple(name for name in SUPPORT_SCENARIOS[1:] if name in recorded)
    for name in expected_scenarios:
        assertion = read(output / f"assertions/{name}.json")
        require(assertion.get("passed") is True and isinstance(assertion.get("checks"), list) and
                assertion["checks"], f"missing support assertions: {name}")
    baseline = read(output / "support-before/status.json")
    for name in ("restored", "reopened"):
        verify_restored_status(read(output / f"support-status/{name}.json"), baseline)
    for name, disabled in (("active", False), ("disabled", True)):
        verify_diagnostic(read(output / f"diagnostics/{name}.json"), disabled)
    for path in (output / "receipts").glob("*.json"):
        require(read(path)["request_id"] == path.stem, "archived support receipt ID differs")
    receipts = [read(path) for path in (output / "receipts").glob("*.json")]
    require(sum(row.get("type") == "restore_install_snapshot" and row.get("status") == "ok"
                for row in receipts) == 1, "installation restore receipt missing or duplicated")
    require(sum(row.get("type") == "offline_code" and row.get("status") == "ok" for row in receipts) == 1,
            "normal business seed receipt missing or duplicated")
    require(read(output / "app-final/setup.json")["phase"] == "restored" and
            (output / "app-final/flags/disable.flag").read_text(encoding="utf-8").strip() == "install_snapshot_restored",
            "restored state did not survive reopen")
    require(digest(output / "app-final/backups/install_pctl_snapshot.json") ==
            digest(output / "support-before/install_pctl_snapshot.json"), "installation snapshot changed")
    require(digest(output / "app-final/ledger/used_nonces.jsonl") ==
            read(output / "assertions/grant.json")["ledger_sha256"], "recovery refunded a consumed nonce")
    rules = read(output / "app-final/rules.json")
    seeded = read(output / "support-before/rules-after-grant.json")
    for key in ("week", "today_override_present", "today_override_day_index", "today_override_minutes"):
        require(rules[key] == seeded[key], "recovery replaced successful business rules")
    prefix = (output / "support-before/activity.jsonl").read_bytes()
    activity = (output / "app-final/activity/history.jsonl").read_bytes()
    require(activity.startswith(prefix), "recovery rewrote previous activity")
    require(sum(decode(line).get("action") == "offline_grant" for line in activity.decode("utf-8").splitlines()) == 1,
            "normal grant activity lost or duplicated")
    require(read(output / "checkpoints/support_diagnostics.json")["ui"]["diagnostic_status"] == 2 and
            read(output / "checkpoints/support_disable.json")["ui"]["disable_flag_present"] is True,
            "support UI evidence differs")
    diagnostics = read(output / "assertions/support_diagnostics.json")
    if "navigation_status_id" in diagnostics:
        status = read(output / f"receipts/{diagnostics['navigation_status_id']}.json")
        events = status["recent_events"][-3:]
        require(status["type"] == "status" and status["status"] == "ok" and events, "missing support event receipts")
        event = read(output / "checkpoints/support-event.json")["ui"]
        guide = read(output / "checkpoints/support-guide.json")["ui"]
        require(event["parent_page"] == guide["parent_page"] == 4 and
                event["overlay"] == 17 and event["overlay_selection"] == len(events) - 1 and
                guide["overlay"] == 43 and guide["selected_index"] == 6 + len(events),
                "support event or guide UI evidence differs")
    if diagnostics.get("version_update_hub") is True:
        for cp_name, expected_fields in (
            ("version-update-online", {"overlay": 46, "update_hub_tab": 0}),
            ("version-update-info", {"overlay": 46, "update_hub_tab": 1, "software_info_qr_tab": 0}),
            ("software-info-repo-gitee", {"overlay": 46, "update_hub_tab": 1,
                                          "software_info_qr_tab": 1, "software_info_repo_source": 1}),
            ("software-info-repo-github", {"overlay": 46, "update_hub_tab": 1,
                                           "software_info_qr_tab": 1, "software_info_repo_source": 0}),
        ):
            cp = read(output / f"checkpoints/{cp_name}.json")["ui"]
            require(all(cp.get(k) == v for k, v in expected_fields.items()),
                    f"version update checkpoint differs: {cp_name}")
    temp_unlock_path = output / "assertions/support_temp_unlock.json"
    if temp_unlock_path.is_file():
        temp_unlock = read(temp_unlock_path)
        unlocked = read(output / f"receipts/{temp_unlock['unlocked_status_id']}.json")
        restored = read(output / f"receipts/{temp_unlock['restored_status_id']}.json")
        require(unlocked["type"] == "status" and unlocked["status"] == "ok" and
                unlocked["state"].get("temporary_unlocked_available") is True and
                unlocked["state"].get("temporary_unlocked") is True,
                "temporary unlock status receipt differs")
        require(restored["type"] == "status" and restored["status"] == "ok" and
                restored["state"].get("temporary_unlocked_available") is True and
                restored["state"].get("temporary_unlocked") is False,
                "restored temporary unlock status receipt differs")
        for cp_name, expected_fields in (
            ("temp-unlock-today", {"view": 1, "parent_page": 0, "overlay": 0,
                                   "temporary_unlocked": True, "parent_footer_status_visible": True}),
            ("temp-unlock-support", {"view": 1, "parent_page": 4, "overlay": 0,
                                     "temporary_unlocked": True, "parent_footer_status_visible": True}),
            ("temp-unlock-child", {"view": 0, "overlay": 0, "temporary_unlocked": True}),
            ("temp-unlock-restored", {"view": 1, "parent_page": 0, "overlay": 0,
                                      "temporary_unlocked": False, "parent_footer_status_visible": False}),
        ):
            cp = read(output / f"checkpoints/{cp_name}.json")["ui"]
            require(all(cp.get(k) == v for k, v in expected_fields.items()),
                    f"temporary unlock checkpoint differs: {cp_name}")


class SupportFlow(MainFlow):
    def reenter_support(self) -> None:
        # Export feedback belongs to its page session. Follow the documented
        # return journey before opening another sensitive support operation.
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        self.page(4)
        self.wait_ui(diagnostic_status=0)

    def archive_status(self, name: str, **expected: object) -> dict:
        result = self.refresh(**expected)
        save(self.output / f"support-status/{name}.json", result)
        return result

    def support_seed(self) -> None:
        self.parent()
        before = self.refresh()
        save(self.output / "support-before/status.json", before)
        shutil.copy2(self.app / "backups/install_pctl_snapshot.json",
                     self.output / "support-before/install_pctl_snapshot.json")
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        self.code_success()
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        save(self.output / "support-before/rules-after-grant.json", read(self.app / "rules.json"))
        target = self.output / "support-before/activity.jsonl"
        shutil.copy2(self.app / "activity/history.jsonl", target)
        self.checked("support_seed", ["normal UI redemption creates a consumed nonce and business activity",
                                      "installation snapshot and observable baseline retained"])

    def export_diagnostic(self, name: str, disabled: bool) -> dict:
        self.page(4)
        status = self.refresh(disable_flag_present=disabled)
        before = set((self.app / "support").glob("diagnostic-*.json"))
        self.action(target="card_4")
        self.confirm()
        self.wait_ui(overlay=0, diagnostic_status=2, waiting=False)
        created = set((self.app / "support").glob("diagnostic-*.json")) - before
        require(len(created) == 1, "diagnostic export did not create exactly one new file")
        source = created.pop()
        verify_diagnostic(read(source), disabled)
        save(self.output / f"diagnostics/{name}.json", read(source))
        save(self.output / f"diagnostics/{name}-identity.json", {"filename": source.name, "sha256": digest(source)})
        return status

    def support_diagnostics(self) -> None:
        status = self.export_diagnostic("active", False)
        events = status["recent_events"][-3:]
        require(events, "seeded support history is missing")
        self.wait_ui(overlay=0, selected_index=4)
        self.action(button="Down")
        self.wait_ui(selected_index=6)
        self.action(button="A")
        self.wait_ui(overlay=17, overlay_selection=len(events) - 1)
        self.capture("support-event")
        self.action(target="overlay_back")
        self.wait_ui(overlay=0, selected_index=6)
        for index in range(7, 7 + len(events)):
            self.action(button="Down")
            self.wait_ui(selected_index=index)
        self.action(button="A")
        self.wait_ui(overlay=43)
        self.capture("support-guide")
        self.action(button="B")
        self.wait_ui(parent_page=4, overlay=0)
        # Verify Version & Update two-tab shell (commit 45246f33): Online Updates -> Version & QR -> Gitee/GitHub -> back.
        self.action(target="card_5")
        self.wait_ui(overlay=46, update_hub_tab=0)
        self.capture("version-update-online")
        self.action(button="R")
        self.wait_ui(overlay=46, update_hub_tab=1, software_info_qr_tab=0)
        self.capture("version-update-info")
        self.action(button="X")
        self.wait_ui(overlay=46, update_hub_tab=1, software_info_qr_tab=1, software_info_repo_source=1)
        self.capture("software-info-repo-gitee")
        self.action(button="ZR")
        self.wait_ui(overlay=46, update_hub_tab=1, software_info_qr_tab=1, software_info_repo_source=0)
        self.capture("software-info-repo-github")
        self.action(button="L")
        self.wait_ui(overlay=46, update_hub_tab=0)
        self.action(button="B")
        self.wait_ui(parent_page=4, overlay=0)
        self.checked("support_diagnostics", ["normal support UI exports readable redacted JSON",
                                             "latest event detail, official guide, and version/update two-tab hub with GitHub/Gitee QR return to support"],
                     navigation_status_id=status["request_id"], version_update_hub=True)

    def support_temp_unlock(self) -> None:
        self.page(0)
        self.wait_ui(view=1, parent_page=0, overlay=0, temporary_unlocked=False)
        fixture = self.app / "support/automation-quota-status.txt"
        fixture.write_text("temp_unlock_on", encoding="ascii")
        unlocked = self.refresh(temporary_unlocked=True, parent_footer_status_visible=True)
        require(unlocked["state"].get("temporary_unlocked_available") is True and
                unlocked["state"].get("temporary_unlocked") is True,
                "simulated temporary unlock was not reflected in status")
        self.wait_ui(view=1, parent_page=0, overlay=0, temporary_unlocked=True, parent_footer_status_visible=True)
        self.capture("temp-unlock-today")
        self.action(target="parent_status_footer")
        self.wait_ui(view=1, parent_page=4, overlay=0, temporary_unlocked=True, parent_footer_status_visible=True)
        self.capture("temp-unlock-support")
        self.action(button="B")
        self.wait_ui(view=0, overlay=0, temporary_unlocked=True)
        self.capture("temp-unlock-child")
        self.page(0)
        fixture.write_text("temp_unlock_off", encoding="ascii")
        restored = self.refresh(temporary_unlocked=False, parent_footer_status_visible=False)
        require(restored["state"].get("temporary_unlocked_available") is True and
                restored["state"].get("temporary_unlocked") is False,
                "clearing simulated temporary unlock did not reflect in status")
        self.capture("temp-unlock-restored")
        self.checked("support_temp_unlock", [
            "simulated temporary unlock exposes footer alert and today overview state",
            "touching footer alert jumps to Support & Recovery health banner",
            "child home reflects temporary unlock and clearing restores normal state",
        ], unlocked_status_id=unlocked["request_id"], restored_status_id=restored["request_id"])

    def support_disable(self) -> None:
        self.reenter_support()
        before = self.ids()
        rules = (self.app / "rules.json").read_bytes()
        self.action(target="card_2")
        self.wait_ui(overlay=3)
        self.action(button="B")
        self.wait_ui(overlay=0, disable_flag_present=False)
        require(not (self.app / "flags/disable.flag").exists(), "cancel created disable flag")
        self.action(target="card_2")
        self.confirm()
        self.wait_ui(overlay=0, disable_flag_present=True)
        require((self.app / "flags/disable.flag").is_file(), "local emergency disable did not persist")
        disabled = self.archive_status("disabled", disable_flag_present=True)
        require(disabled["setup"]["disable_flag_present"] is True, "backend did not observe local disable flag")
        self.page(0)
        self.action(target="card_2")  # The disabled quick-grant entry must not create a request.
        self.wait_ui(overlay=0, waiting=False, disable_flag_present=True)
        self.refresh(disable_flag_present=True)
        require((self.app / "rules.json").read_bytes() == rules and
                (self.app / "ledger/used_nonces.jsonl").read_bytes() == self.ledger,
                "disabled action changed rules or consumed nonce")
        new_results = [read(path) for path in (self.app / "results").glob("*.json") if path.stem not in before]
        require(all(row.get("type") == "status" for row in new_results), "disabled quick grant submitted a write")
        self.export_diagnostic("disabled", True)
        self.checked("support_disable", ["cancel leaves control enabled", "confirmation persists local disable flag",
                                         "status and diagnostic export remain reachable", "disabled quick grant sends no write"])

    def support_restore(self) -> None:
        self.reenter_support()
        before = self.ids()
        self.action(target="card_3")
        self.wait_ui(overlay=44)
        self.action(button="X")
        self.wait_ui(overlay=3)
        self.action(button="B")
        self.wait_ui(overlay=44, disable_flag_present=True)
        self.action(button="B")
        self.wait_ui(overlay=0, disable_flag_present=True)
        require(read(self.app / "setup.json")["phase"] != "restored", "cancel restored installation")
        require(not any(read(path).get("type") == "restore_install_snapshot"
                        for path in (self.app / "results").glob("*.json") if path.stem not in before),
                "cancel submitted installation restore")
        self.action(target="card_3")
        self.wait_ui(overlay=44)
        self.action(button="X")
        self.submitted("restore_install_snapshot", before)
        result = self.archive_status("restored", disable_flag_present=True, recovery_active=False, setup_active=False)
        verify_restored_status(result, read(self.output / "support-before/status.json"))
        require((self.app / "ledger/used_nonces.jsonl").read_bytes() == self.ledger, "restore refunded consumed nonce")
        self.checked("support_restore", ["cancel creates no restore transaction", "restore works while disabled",
                                         "fresh status shows restored baseline without active recovery",
                                         "successful business nonce remains consumed"])

    def support_reopen(self) -> None:
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0, disable_flag_present=True, setup_active=False)
        self.capture("restored-child-reopened")
        self.page(4)  # Re-enter through normal parent PIN, without reactivating control.
        result = self.archive_status("reopened", disable_flag_present=True, recovery_active=False)
        verify_restored_status(result, read(self.output / "support-before/status.json"))
        require((self.app / "ledger/used_nonces.jsonl").read_bytes() == self.ledger, "reopen lost business nonce")
        self.checked("support_reopen", ["new instance remains restored and disabled", "support/status reachable with normal PIN",
                                        "restored simulated baseline and normal business traces persist"])
